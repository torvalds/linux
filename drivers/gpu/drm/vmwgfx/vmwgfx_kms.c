// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright (c) 2009-2025 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 *
 **************************************************************************/

#include "vmwgfx_kms.h"

#include "vmwgfx_bo.h"
#include "vmwgfx_resource_priv.h"
#include "vmwgfx_vkms.h"
#include "vmw_surface_cache.h"

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_rect.h>
#include <drm/drm_sysfs.h>
#include <drm/drm_edid.h>

void vmw_du_init(struct vmw_display_unit *du)
{
	vmw_vkms_crtc_init(&du->crtc);
}

void vmw_du_cleanup(struct vmw_display_unit *du)
{
	struct vmw_private *dev_priv = vmw_priv(du->primary.dev);

	vmw_vkms_crtc_cleanup(&du->crtc);
	drm_plane_cleanup(&du->primary);
	if (vmw_cmd_supported(dev_priv))
		drm_plane_cleanup(&du->cursor.base);

	drm_connector_unregister(&du->connector);
	drm_crtc_cleanup(&du->crtc);
	drm_encoder_cleanup(&du->encoder);
	drm_connector_cleanup(&du->connector);
}


void vmw_du_primary_plane_destroy(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);

	/* Planes are static in our case so we don't free it */
}


/**
 * vmw_du_plane_unpin_surf - unpins resource associated with a framebuffer surface
 *
 * @vps: plane state associated with the display surface
 */
void vmw_du_plane_unpin_surf(struct vmw_plane_state *vps)
{
	struct vmw_surface *surf = vmw_user_object_surface(&vps->uo);

	if (surf) {
		if (vps->pinned) {
			vmw_resource_unpin(&surf->res);
			vps->pinned--;
		}
	}
}


/**
 * vmw_du_plane_cleanup_fb - Unpins the plane surface
 *
 * @plane:  display plane
 * @old_state: Contains the FB to clean up
 *
 * Unpins the framebuffer surface
 *
 * Returns 0 on success
 */
void
vmw_du_plane_cleanup_fb(struct drm_plane *plane,
			struct drm_plane_state *old_state)
{
	struct vmw_plane_state *vps = vmw_plane_state_to_vps(old_state);

	vmw_du_plane_unpin_surf(vps);
}


/**
 * vmw_du_primary_plane_atomic_check - check if the new state is okay
 *
 * @plane: display plane
 * @state: info on the new plane state, including the FB
 *
 * Check if the new state is settable given the current state.  Other
 * than what the atomic helper checks, we care about crtc fitting
 * the FB and maintaining one active framebuffer.
 *
 * Returns 0 on success
 */
int vmw_du_primary_plane_atomic_check(struct drm_plane *plane,
				      struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state,
									   plane);
	struct drm_crtc_state *crtc_state = NULL;
	struct drm_framebuffer *new_fb = new_state->fb;
	struct drm_framebuffer *old_fb = old_state->fb;
	int ret;

	/*
	 * Ignore damage clips if the framebuffer attached to the plane's state
	 * has changed since the last plane update (page-flip). In this case, a
	 * full plane update should happen because uploads are done per-buffer.
	 */
	if (old_fb != new_fb)
		new_state->ignore_damage_clips = true;

	if (new_state->crtc)
		crtc_state = drm_atomic_get_new_crtc_state(state,
							   new_state->crtc);

	ret = drm_atomic_helper_check_plane_state(new_state, crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  false, true);
	return ret;
}

int vmw_du_crtc_atomic_check(struct drm_crtc *crtc,
			     struct drm_atomic_state *state)
{
	struct vmw_private *vmw = vmw_priv(crtc->dev);
	struct drm_crtc_state *new_state = drm_atomic_get_new_crtc_state(state,
									 crtc);
	struct vmw_display_unit *du = vmw_crtc_to_du(new_state->crtc);
	int connector_mask = drm_connector_mask(&du->connector);
	bool has_primary = new_state->plane_mask &
			   drm_plane_mask(crtc->primary);

	/*
	 * This is fine in general, but broken userspace might expect
	 * some actual rendering so give a clue as why it's blank.
	 */
	if (new_state->enable && !has_primary)
		drm_dbg_driver(&vmw->drm,
			       "CRTC without a primary plane will be blank.\n");


	if (new_state->connector_mask != connector_mask &&
	    new_state->connector_mask != 0) {
		DRM_ERROR("Invalid connectors configuration\n");
		return -EINVAL;
	}

	/*
	 * Our virtual device does not have a dot clock, so use the logical
	 * clock value as the dot clock.
	 */
	if (new_state->mode.crtc_clock == 0)
		new_state->adjusted_mode.crtc_clock = new_state->mode.clock;

	return 0;
}


void vmw_du_crtc_atomic_begin(struct drm_crtc *crtc,
			      struct drm_atomic_state *state)
{
	vmw_vkms_crtc_atomic_begin(crtc, state);
}

/**
 * vmw_du_crtc_duplicate_state - duplicate crtc state
 * @crtc: DRM crtc
 *
 * Allocates and returns a copy of the crtc state (both common and
 * vmw-specific) for the specified crtc.
 *
 * Returns: The newly allocated crtc state, or NULL on failure.
 */
struct drm_crtc_state *
vmw_du_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct drm_crtc_state *state;
	struct vmw_crtc_state *vcs;

	if (WARN_ON(!crtc->state))
		return NULL;

	vcs = kmemdup(crtc->state, sizeof(*vcs), GFP_KERNEL);

	if (!vcs)
		return NULL;

	state = &vcs->base;

	__drm_atomic_helper_crtc_duplicate_state(crtc, state);

	return state;
}


/**
 * vmw_du_crtc_reset - creates a blank vmw crtc state
 * @crtc: DRM crtc
 *
 * Resets the atomic state for @crtc by freeing the state pointer (which
 * might be NULL, e.g. at driver load time) and allocating a new empty state
 * object.
 */
void vmw_du_crtc_reset(struct drm_crtc *crtc)
{
	struct vmw_crtc_state *vcs;


	if (crtc->state) {
		__drm_atomic_helper_crtc_destroy_state(crtc->state);

		kfree(vmw_crtc_state_to_vcs(crtc->state));
	}

	vcs = kzalloc(sizeof(*vcs), GFP_KERNEL);

	if (!vcs) {
		DRM_ERROR("Cannot allocate vmw_crtc_state\n");
		return;
	}

	__drm_atomic_helper_crtc_reset(crtc, &vcs->base);
}


/**
 * vmw_du_crtc_destroy_state - destroy crtc state
 * @crtc: DRM crtc
 * @state: state object to destroy
 *
 * Destroys the crtc state (both common and vmw-specific) for the
 * specified plane.
 */
void
vmw_du_crtc_destroy_state(struct drm_crtc *crtc,
			  struct drm_crtc_state *state)
{
	drm_atomic_helper_crtc_destroy_state(crtc, state);
}


/**
 * vmw_du_plane_duplicate_state - duplicate plane state
 * @plane: drm plane
 *
 * Allocates and returns a copy of the plane state (both common and
 * vmw-specific) for the specified plane.
 *
 * Returns: The newly allocated plane state, or NULL on failure.
 */
struct drm_plane_state *
vmw_du_plane_duplicate_state(struct drm_plane *plane)
{
	struct drm_plane_state *state;
	struct vmw_plane_state *vps;

	vps = kmemdup(plane->state, sizeof(*vps), GFP_KERNEL);

	if (!vps)
		return NULL;

	vps->pinned = 0;
	vps->cpp = 0;

	vps->cursor.mob = NULL;

	/* Each ref counted resource needs to be acquired again */
	vmw_user_object_ref(&vps->uo);
	state = &vps->base;

	__drm_atomic_helper_plane_duplicate_state(plane, state);

	return state;
}


/**
 * vmw_du_plane_reset - creates a blank vmw plane state
 * @plane: drm plane
 *
 * Resets the atomic state for @plane by freeing the state pointer (which might
 * be NULL, e.g. at driver load time) and allocating a new empty state object.
 */
void vmw_du_plane_reset(struct drm_plane *plane)
{
	struct vmw_plane_state *vps;

	if (plane->state)
		vmw_du_plane_destroy_state(plane, plane->state);

	vps = kzalloc(sizeof(*vps), GFP_KERNEL);

	if (!vps) {
		DRM_ERROR("Cannot allocate vmw_plane_state\n");
		return;
	}

	__drm_atomic_helper_plane_reset(plane, &vps->base);
}


/**
 * vmw_du_plane_destroy_state - destroy plane state
 * @plane: DRM plane
 * @state: state object to destroy
 *
 * Destroys the plane state (both common and vmw-specific) for the
 * specified plane.
 */
void
vmw_du_plane_destroy_state(struct drm_plane *plane,
			   struct drm_plane_state *state)
{
	struct vmw_plane_state *vps = vmw_plane_state_to_vps(state);

	/* Should have been freed by cleanup_fb */
	vmw_user_object_unref(&vps->uo);

	drm_atomic_helper_plane_destroy_state(plane, state);
}


/**
 * vmw_du_connector_duplicate_state - duplicate connector state
 * @connector: DRM connector
 *
 * Allocates and returns a copy of the connector state (both common and
 * vmw-specific) for the specified connector.
 *
 * Returns: The newly allocated connector state, or NULL on failure.
 */
struct drm_connector_state *
vmw_du_connector_duplicate_state(struct drm_connector *connector)
{
	struct drm_connector_state *state;
	struct vmw_connector_state *vcs;

	if (WARN_ON(!connector->state))
		return NULL;

	vcs = kmemdup(connector->state, sizeof(*vcs), GFP_KERNEL);

	if (!vcs)
		return NULL;

	state = &vcs->base;

	__drm_atomic_helper_connector_duplicate_state(connector, state);

	return state;
}


/**
 * vmw_du_connector_reset - creates a blank vmw connector state
 * @connector: DRM connector
 *
 * Resets the atomic state for @connector by freeing the state pointer (which
 * might be NULL, e.g. at driver load time) and allocating a new empty state
 * object.
 */
void vmw_du_connector_reset(struct drm_connector *connector)
{
	struct vmw_connector_state *vcs;


	if (connector->state) {
		__drm_atomic_helper_connector_destroy_state(connector->state);

		kfree(vmw_connector_state_to_vcs(connector->state));
	}

	vcs = kzalloc(sizeof(*vcs), GFP_KERNEL);

	if (!vcs) {
		DRM_ERROR("Cannot allocate vmw_connector_state\n");
		return;
	}

	__drm_atomic_helper_connector_reset(connector, &vcs->base);
}


/**
 * vmw_du_connector_destroy_state - destroy connector state
 * @connector: DRM connector
 * @state: state object to destroy
 *
 * Destroys the connector state (both common and vmw-specific) for the
 * specified plane.
 */
void
vmw_du_connector_destroy_state(struct drm_connector *connector,
			  struct drm_connector_state *state)
{
	drm_atomic_helper_connector_destroy_state(connector, state);
}
/*
 * Generic framebuffer code
 */

/*
 * Surface framebuffer code
 */

static void vmw_framebuffer_surface_destroy(struct drm_framebuffer *framebuffer)
{
	struct vmw_framebuffer_surface *vfbs =
		vmw_framebuffer_to_vfbs(framebuffer);
	struct vmw_bo *bo = vmw_user_object_buffer(&vfbs->uo);
	struct vmw_surface *surf = vmw_user_object_surface(&vfbs->uo);

	if (bo) {
		vmw_bo_dirty_release(bo);
		/*
		 * bo->dirty is reference counted so it being NULL
		 * means that the surface wasn't coherent to begin
		 * with and so we have to free the dirty tracker
		 * in the vmw_resource
		 */
		if (!bo->dirty && surf && surf->res.dirty)
			surf->res.func->dirty_free(&surf->res);
	}
	drm_framebuffer_cleanup(framebuffer);
	vmw_user_object_unref(&vfbs->uo);

	kfree(vfbs);
}

/**
 * vmw_kms_readback - Perform a readback from the screen system to
 * a buffer-object backed framebuffer.
 *
 * @dev_priv: Pointer to the device private structure.
 * @file_priv: Pointer to a struct drm_file identifying the caller.
 * Must be set to NULL if @user_fence_rep is NULL.
 * @vfb: Pointer to the buffer-object backed framebuffer.
 * @user_fence_rep: User-space provided structure for fence information.
 * Must be set to non-NULL if @file_priv is non-NULL.
 * @vclips: Array of clip rects.
 * @num_clips: Number of clip rects in @vclips.
 *
 * Returns 0 on success, negative error code on failure. -ERESTARTSYS if
 * interrupted.
 */
int vmw_kms_readback(struct vmw_private *dev_priv,
		     struct drm_file *file_priv,
		     struct vmw_framebuffer *vfb,
		     struct drm_vmw_fence_rep __user *user_fence_rep,
		     struct drm_vmw_rect *vclips,
		     uint32_t num_clips)
{
	switch (dev_priv->active_display_unit) {
	case vmw_du_screen_object:
		return vmw_kms_sou_readback(dev_priv, file_priv, vfb,
					    user_fence_rep, vclips, num_clips,
					    NULL);
	case vmw_du_screen_target:
		return vmw_kms_stdu_readback(dev_priv, file_priv, vfb,
					     user_fence_rep, NULL, vclips, num_clips,
					     1, NULL);
	default:
		WARN_ONCE(true,
			  "Readback called with invalid display system.\n");
}

	return -ENOSYS;
}

static int vmw_framebuffer_surface_create_handle(struct drm_framebuffer *fb,
						 struct drm_file *file_priv,
						 unsigned int *handle)
{
	struct vmw_framebuffer_surface *vfbs = vmw_framebuffer_to_vfbs(fb);
	struct vmw_bo *bo = vmw_user_object_buffer(&vfbs->uo);

	if (WARN_ON(!bo))
		return -EINVAL;
	return drm_gem_handle_create(file_priv, &bo->tbo.base, handle);
}

static const struct drm_framebuffer_funcs vmw_framebuffer_surface_funcs = {
	.create_handle = vmw_framebuffer_surface_create_handle,
	.destroy = vmw_framebuffer_surface_destroy,
	.dirty = drm_atomic_helper_dirtyfb,
};

static int vmw_kms_new_framebuffer_surface(struct vmw_private *dev_priv,
					   struct vmw_user_object *uo,
					   struct vmw_framebuffer **out,
					   const struct drm_format_info *info,
					   const struct drm_mode_fb_cmd2
					   *mode_cmd)

{
	struct drm_device *dev = &dev_priv->drm;
	struct vmw_framebuffer_surface *vfbs;
	struct vmw_surface *surface;
	int ret;

	/* 3D is only supported on HWv8 and newer hosts */
	if (dev_priv->active_display_unit == vmw_du_legacy)
		return -ENOSYS;

	surface = vmw_user_object_surface(uo);

	/*
	 * Sanity checks.
	 */

	if (!drm_any_plane_has_format(&dev_priv->drm,
				      mode_cmd->pixel_format,
				      mode_cmd->modifier[0])) {
		drm_dbg(&dev_priv->drm,
			"unsupported pixel format %p4cc / modifier 0x%llx\n",
			&mode_cmd->pixel_format, mode_cmd->modifier[0]);
		return -EINVAL;
	}

	/* Surface must be marked as a scanout. */
	if (unlikely(!surface->metadata.scanout))
		return -EINVAL;

	if (unlikely(surface->metadata.mip_levels[0] != 1 ||
		     surface->metadata.num_sizes != 1 ||
		     surface->metadata.base_size.width < mode_cmd->width ||
		     surface->metadata.base_size.height < mode_cmd->height ||
		     surface->metadata.base_size.depth != 1)) {
		DRM_ERROR("Incompatible surface dimensions "
			  "for requested mode.\n");
		return -EINVAL;
	}

	vfbs = kzalloc(sizeof(*vfbs), GFP_KERNEL);
	if (!vfbs) {
		ret = -ENOMEM;
		goto out_err1;
	}

	drm_helper_mode_fill_fb_struct(dev, &vfbs->base.base, info, mode_cmd);
	memcpy(&vfbs->uo, uo, sizeof(vfbs->uo));
	vmw_user_object_ref(&vfbs->uo);

	*out = &vfbs->base;

	ret = drm_framebuffer_init(dev, &vfbs->base.base,
				   &vmw_framebuffer_surface_funcs);
	if (ret)
		goto out_err2;

	return 0;

out_err2:
	vmw_user_object_unref(&vfbs->uo);
	kfree(vfbs);
out_err1:
	return ret;
}

/*
 * Buffer-object framebuffer code
 */

static int vmw_framebuffer_bo_create_handle(struct drm_framebuffer *fb,
					    struct drm_file *file_priv,
					    unsigned int *handle)
{
	struct vmw_framebuffer_bo *vfbd =
			vmw_framebuffer_to_vfbd(fb);
	return drm_gem_handle_create(file_priv, &vfbd->buffer->tbo.base, handle);
}

static void vmw_framebuffer_bo_destroy(struct drm_framebuffer *framebuffer)
{
	struct vmw_framebuffer_bo *vfbd =
		vmw_framebuffer_to_vfbd(framebuffer);

	vmw_bo_dirty_release(vfbd->buffer);
	drm_framebuffer_cleanup(framebuffer);
	vmw_bo_unreference(&vfbd->buffer);

	kfree(vfbd);
}

static const struct drm_framebuffer_funcs vmw_framebuffer_bo_funcs = {
	.create_handle = vmw_framebuffer_bo_create_handle,
	.destroy = vmw_framebuffer_bo_destroy,
	.dirty = drm_atomic_helper_dirtyfb,
};

static int vmw_kms_new_framebuffer_bo(struct vmw_private *dev_priv,
				      struct vmw_bo *bo,
				      struct vmw_framebuffer **out,
				      const struct drm_format_info *info,
				      const struct drm_mode_fb_cmd2
				      *mode_cmd)

{
	struct drm_device *dev = &dev_priv->drm;
	struct vmw_framebuffer_bo *vfbd;
	unsigned int requested_size;
	int ret;

	requested_size = mode_cmd->height * mode_cmd->pitches[0];
	if (unlikely(requested_size > bo->tbo.base.size)) {
		DRM_ERROR("Screen buffer object size is too small "
			  "for requested mode.\n");
		return -EINVAL;
	}

	if (!drm_any_plane_has_format(&dev_priv->drm,
				      mode_cmd->pixel_format,
				      mode_cmd->modifier[0])) {
		drm_dbg(&dev_priv->drm,
			"unsupported pixel format %p4cc / modifier 0x%llx\n",
			&mode_cmd->pixel_format, mode_cmd->modifier[0]);
		return -EINVAL;
	}

	vfbd = kzalloc(sizeof(*vfbd), GFP_KERNEL);
	if (!vfbd) {
		ret = -ENOMEM;
		goto out_err1;
	}

	vfbd->base.base.obj[0] = &bo->tbo.base;
	drm_helper_mode_fill_fb_struct(dev, &vfbd->base.base, info, mode_cmd);
	vfbd->base.bo = true;
	vfbd->buffer = vmw_bo_reference(bo);
	*out = &vfbd->base;

	ret = drm_framebuffer_init(dev, &vfbd->base.base,
				   &vmw_framebuffer_bo_funcs);
	if (ret)
		goto out_err2;

	return 0;

out_err2:
	vmw_bo_unreference(&bo);
	kfree(vfbd);
out_err1:
	return ret;
}


/**
 * vmw_kms_srf_ok - check if a surface can be created
 *
 * @dev_priv: Pointer to device private struct.
 * @width: requested width
 * @height: requested height
 *
 * Surfaces need to be less than texture size
 */
static bool
vmw_kms_srf_ok(struct vmw_private *dev_priv, uint32_t width, uint32_t height)
{
	if (width  > dev_priv->texture_max_width ||
	    height > dev_priv->texture_max_height)
		return false;

	return true;
}

/**
 * vmw_kms_new_framebuffer - Create a new framebuffer.
 *
 * @dev_priv: Pointer to device private struct.
 * @uo: Pointer to user object to wrap the kms framebuffer around.
 * Either the buffer or surface inside the user object must be NULL.
 * @info: pixel format information.
 * @mode_cmd: Frame-buffer metadata.
 */
struct vmw_framebuffer *
vmw_kms_new_framebuffer(struct vmw_private *dev_priv,
			struct vmw_user_object *uo,
			const struct drm_format_info *info,
			const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct vmw_framebuffer *vfb = NULL;
	int ret;

	/* Create the new framebuffer depending one what we have */
	if (vmw_user_object_surface(uo)) {
		ret = vmw_kms_new_framebuffer_surface(dev_priv, uo, &vfb,
						      info, mode_cmd);
	} else if (uo->buffer) {
		ret = vmw_kms_new_framebuffer_bo(dev_priv, uo->buffer, &vfb,
						 info, mode_cmd);
	} else {
		BUG();
	}

	if (ret)
		return ERR_PTR(ret);

	return vfb;
}

/*
 * Generic Kernel modesetting functions
 */

static struct drm_framebuffer *vmw_kms_fb_create(struct drm_device *dev,
						 struct drm_file *file_priv,
						 const struct drm_format_info *info,
						 const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_framebuffer *vfb = NULL;
	struct vmw_user_object uo = {0};
	struct vmw_bo *bo;
	struct vmw_surface *surface;
	int ret;

	/* returns either a bo or surface */
	ret = vmw_user_object_lookup(dev_priv, file_priv, mode_cmd->handles[0],
				     &uo);
	if (ret) {
		DRM_ERROR("Invalid buffer object handle %u (0x%x).\n",
			  mode_cmd->handles[0], mode_cmd->handles[0]);
		goto err_out;
	}


	if (vmw_user_object_surface(&uo) &&
	    !vmw_kms_srf_ok(dev_priv, mode_cmd->width, mode_cmd->height)) {
		DRM_ERROR("Surface size cannot exceed %dx%d\n",
			dev_priv->texture_max_width,
			dev_priv->texture_max_height);
		ret = -EINVAL;
		goto err_out;
	}


	vfb = vmw_kms_new_framebuffer(dev_priv, &uo, info, mode_cmd);
	if (IS_ERR(vfb)) {
		ret = PTR_ERR(vfb);
		goto err_out;
	}

err_out:
	bo = vmw_user_object_buffer(&uo);
	surface = vmw_user_object_surface(&uo);
	/* vmw_user_object_lookup takes one ref so does new_fb */
	vmw_user_object_unref(&uo);

	if (ret) {
		DRM_ERROR("failed to create vmw_framebuffer: %i\n", ret);
		return ERR_PTR(ret);
	}

	ttm_bo_reserve(&bo->tbo, false, false, NULL);
	ret = vmw_bo_dirty_add(bo);
	if (!ret && surface && surface->res.func->dirty_alloc) {
		surface->res.coherent = true;
		ret = surface->res.func->dirty_alloc(&surface->res);
	}
	ttm_bo_unreserve(&bo->tbo);

	return &vfb->base;
}

/**
 * vmw_kms_check_display_memory - Validates display memory required for a
 * topology
 * @dev: DRM device
 * @num_rects: number of drm_rect in rects
 * @rects: array of drm_rect representing the topology to validate indexed by
 * crtc index.
 *
 * Returns:
 * 0 on success otherwise negative error code
 */
static int vmw_kms_check_display_memory(struct drm_device *dev,
					uint32_t num_rects,
					struct drm_rect *rects)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct drm_rect bounding_box = {0};
	u64 total_pixels = 0, pixel_mem, bb_mem;
	int i;

	for (i = 0; i < num_rects; i++) {
		/*
		 * For STDU only individual screen (screen target) is limited by
		 * SCREENTARGET_MAX_WIDTH/HEIGHT registers.
		 */
		if (dev_priv->active_display_unit == vmw_du_screen_target &&
		    (drm_rect_width(&rects[i]) > dev_priv->stdu_max_width ||
		     drm_rect_height(&rects[i]) > dev_priv->stdu_max_height)) {
			VMW_DEBUG_KMS("Screen size not supported.\n");
			return -EINVAL;
		}

		/* Bounding box upper left is at (0,0). */
		if (rects[i].x2 > bounding_box.x2)
			bounding_box.x2 = rects[i].x2;

		if (rects[i].y2 > bounding_box.y2)
			bounding_box.y2 = rects[i].y2;

		total_pixels += (u64) drm_rect_width(&rects[i]) *
			(u64) drm_rect_height(&rects[i]);
	}

	/* Virtual svga device primary limits are always in 32-bpp. */
	pixel_mem = total_pixels * 4;

	/*
	 * For HV10 and below prim_bb_mem is vram size. When
	 * SVGA_REG_MAX_PRIMARY_BOUNDING_BOX_MEM is not present vram size is
	 * limit on primary bounding box
	 */
	if (pixel_mem > dev_priv->max_primary_mem) {
		VMW_DEBUG_KMS("Combined output size too large.\n");
		return -EINVAL;
	}

	/* SVGA_CAP_NO_BB_RESTRICTION is available for STDU only. */
	if (dev_priv->active_display_unit != vmw_du_screen_target ||
	    !(dev_priv->capabilities & SVGA_CAP_NO_BB_RESTRICTION)) {
		bb_mem = (u64) bounding_box.x2 * bounding_box.y2 * 4;

		if (bb_mem > dev_priv->max_primary_mem) {
			VMW_DEBUG_KMS("Topology is beyond supported limits.\n");
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * vmw_crtc_state_and_lock - Return new or current crtc state with locked
 * crtc mutex
 * @state: The atomic state pointer containing the new atomic state
 * @crtc: The crtc
 *
 * This function returns the new crtc state if it's part of the state update.
 * Otherwise returns the current crtc state. It also makes sure that the
 * crtc mutex is locked.
 *
 * Returns: A valid crtc state pointer or NULL. It may also return a
 * pointer error, in particular -EDEADLK if locking needs to be rerun.
 */
static struct drm_crtc_state *
vmw_crtc_state_and_lock(struct drm_atomic_state *state, struct drm_crtc *crtc)
{
	struct drm_crtc_state *crtc_state;

	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	if (crtc_state) {
		lockdep_assert_held(&crtc->mutex.mutex.base);
	} else {
		int ret = drm_modeset_lock(&crtc->mutex, state->acquire_ctx);

		if (ret != 0 && ret != -EALREADY)
			return ERR_PTR(ret);

		crtc_state = crtc->state;
	}

	return crtc_state;
}

/**
 * vmw_kms_check_implicit - Verify that all implicit display units scan out
 * from the same fb after the new state is committed.
 * @dev: The drm_device.
 * @state: The new state to be checked.
 *
 * Returns:
 *   Zero on success,
 *   -EINVAL on invalid state,
 *   -EDEADLK if modeset locking needs to be rerun.
 */
static int vmw_kms_check_implicit(struct drm_device *dev,
				  struct drm_atomic_state *state)
{
	struct drm_framebuffer *implicit_fb = NULL;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_plane_state *plane_state;

	drm_for_each_crtc(crtc, dev) {
		struct vmw_display_unit *du = vmw_crtc_to_du(crtc);

		if (!du->is_implicit)
			continue;

		crtc_state = vmw_crtc_state_and_lock(state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		if (!crtc_state || !crtc_state->enable)
			continue;

		/*
		 * Can't move primary planes across crtcs, so this is OK.
		 * It also means we don't need to take the plane mutex.
		 */
		plane_state = du->primary.state;
		if (plane_state->crtc != crtc)
			continue;

		if (!implicit_fb)
			implicit_fb = plane_state->fb;
		else if (implicit_fb != plane_state->fb)
			return -EINVAL;
	}

	return 0;
}

/**
 * vmw_kms_check_topology - Validates topology in drm_atomic_state
 * @dev: DRM device
 * @state: the driver state object
 *
 * Returns:
 * 0 on success otherwise negative error code
 */
static int vmw_kms_check_topology(struct drm_device *dev,
				  struct drm_atomic_state *state)
{
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_rect *rects;
	struct drm_crtc *crtc;
	uint32_t i;
	int ret = 0;

	rects = kcalloc(dev->mode_config.num_crtc, sizeof(struct drm_rect),
			GFP_KERNEL);
	if (!rects)
		return -ENOMEM;

	drm_for_each_crtc(crtc, dev) {
		struct vmw_display_unit *du = vmw_crtc_to_du(crtc);
		struct drm_crtc_state *crtc_state;

		i = drm_crtc_index(crtc);

		crtc_state = vmw_crtc_state_and_lock(state, crtc);
		if (IS_ERR(crtc_state)) {
			ret = PTR_ERR(crtc_state);
			goto clean;
		}

		if (!crtc_state)
			continue;

		if (crtc_state->enable) {
			rects[i].x1 = du->gui_x;
			rects[i].y1 = du->gui_y;
			rects[i].x2 = du->gui_x + crtc_state->mode.hdisplay;
			rects[i].y2 = du->gui_y + crtc_state->mode.vdisplay;
		} else {
			rects[i].x1 = 0;
			rects[i].y1 = 0;
			rects[i].x2 = 0;
			rects[i].y2 = 0;
		}
	}

	/* Determine change to topology due to new atomic state */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state,
				      new_crtc_state, i) {
		struct vmw_display_unit *du = vmw_crtc_to_du(crtc);
		struct drm_connector *connector;
		struct drm_connector_state *conn_state;
		struct vmw_connector_state *vmw_conn_state;

		if (!du->pref_active && new_crtc_state->enable) {
			VMW_DEBUG_KMS("Enabling a disabled display unit\n");
			ret = -EINVAL;
			goto clean;
		}

		/*
		 * For vmwgfx each crtc has only one connector attached and it
		 * is not changed so don't really need to check the
		 * crtc->connector_mask and iterate over it.
		 */
		connector = &du->connector;
		conn_state = drm_atomic_get_connector_state(state, connector);
		if (IS_ERR(conn_state)) {
			ret = PTR_ERR(conn_state);
			goto clean;
		}

		vmw_conn_state = vmw_connector_state_to_vcs(conn_state);
		vmw_conn_state->gui_x = du->gui_x;
		vmw_conn_state->gui_y = du->gui_y;
	}

	ret = vmw_kms_check_display_memory(dev, dev->mode_config.num_crtc,
					   rects);

clean:
	kfree(rects);
	return ret;
}

/**
 * vmw_kms_atomic_check_modeset- validate state object for modeset changes
 *
 * @dev: DRM device
 * @state: the driver state object
 *
 * This is a simple wrapper around drm_atomic_helper_check_modeset() for
 * us to assign a value to mode->crtc_clock so that
 * drm_calc_timestamping_constants() won't throw an error message
 *
 * Returns:
 * Zero for success or -errno
 */
static int
vmw_kms_atomic_check_modeset(struct drm_device *dev,
			     struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	bool need_modeset = false;
	int i, ret;

	ret = drm_atomic_helper_check(dev, state);
	if (ret)
		return ret;

	ret = vmw_kms_check_implicit(dev, state);
	if (ret) {
		VMW_DEBUG_KMS("Invalid implicit state\n");
		return ret;
	}

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		if (drm_atomic_crtc_needs_modeset(crtc_state))
			need_modeset = true;
	}

	if (need_modeset)
		return vmw_kms_check_topology(dev, state);

	return ret;
}

static const struct drm_mode_config_funcs vmw_kms_funcs = {
	.fb_create = vmw_kms_fb_create,
	.atomic_check = vmw_kms_atomic_check_modeset,
	.atomic_commit = drm_atomic_helper_commit,
};

static int vmw_kms_generic_present(struct vmw_private *dev_priv,
				   struct drm_file *file_priv,
				   struct vmw_framebuffer *vfb,
				   struct vmw_surface *surface,
				   uint32_t sid,
				   int32_t destX, int32_t destY,
				   struct drm_vmw_rect *clips,
				   uint32_t num_clips)
{
	return vmw_kms_sou_do_surface_dirty(dev_priv, vfb, NULL, clips,
					    &surface->res, destX, destY,
					    num_clips, 1, NULL, NULL);
}


int vmw_kms_present(struct vmw_private *dev_priv,
		    struct drm_file *file_priv,
		    struct vmw_framebuffer *vfb,
		    struct vmw_surface *surface,
		    uint32_t sid,
		    int32_t destX, int32_t destY,
		    struct drm_vmw_rect *clips,
		    uint32_t num_clips)
{
	int ret;

	switch (dev_priv->active_display_unit) {
	case vmw_du_screen_target:
		ret = vmw_kms_stdu_surface_dirty(dev_priv, vfb, NULL, clips,
						 &surface->res, destX, destY,
						 num_clips, 1, NULL, NULL);
		break;
	case vmw_du_screen_object:
		ret = vmw_kms_generic_present(dev_priv, file_priv, vfb, surface,
					      sid, destX, destY, clips,
					      num_clips);
		break;
	default:
		WARN_ONCE(true,
			  "Present called with invalid display system.\n");
		ret = -ENOSYS;
		break;
	}
	if (ret)
		return ret;

	vmw_cmd_flush(dev_priv, false);

	return 0;
}

static void
vmw_kms_create_hotplug_mode_update_property(struct vmw_private *dev_priv)
{
	if (dev_priv->hotplug_mode_update_property)
		return;

	dev_priv->hotplug_mode_update_property =
		drm_property_create_range(&dev_priv->drm,
					  DRM_MODE_PROP_IMMUTABLE,
					  "hotplug_mode_update", 0, 1);
}

static void
vmw_atomic_commit_tail(struct drm_atomic_state *old_state)
{
	struct vmw_private *vmw = vmw_priv(old_state->dev);
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	int i;

	drm_atomic_helper_commit_tail(old_state);

	if (vmw->vkms_enabled) {
		for_each_old_crtc_in_state(old_state, crtc, old_crtc_state, i) {
			struct vmw_display_unit *du = vmw_crtc_to_du(crtc);
			(void)old_crtc_state;
			flush_work(&du->vkms.crc_generator_work);
		}
	}
}

static const struct drm_mode_config_helper_funcs vmw_mode_config_helpers = {
	.atomic_commit_tail = vmw_atomic_commit_tail,
};

int vmw_kms_init(struct vmw_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	int ret;
	static const char *display_unit_names[] = {
		"Invalid",
		"Legacy",
		"Screen Object",
		"Screen Target",
		"Invalid (max)"
	};

	drm_mode_config_init(dev);
	dev->mode_config.funcs = &vmw_kms_funcs;
	dev->mode_config.min_width = 1;
	dev->mode_config.min_height = 1;
	dev->mode_config.max_width = dev_priv->texture_max_width;
	dev->mode_config.max_height = dev_priv->texture_max_height;
	dev->mode_config.preferred_depth = dev_priv->assume_16bpp ? 16 : 32;
	dev->mode_config.helper_private = &vmw_mode_config_helpers;

	drm_mode_create_suggested_offset_properties(dev);
	vmw_kms_create_hotplug_mode_update_property(dev_priv);

	ret = vmw_kms_stdu_init_display(dev_priv);
	if (ret) {
		ret = vmw_kms_sou_init_display(dev_priv);
		if (ret) /* Fallback */
			ret = vmw_kms_ldu_init_display(dev_priv);
	}
	BUILD_BUG_ON(ARRAY_SIZE(display_unit_names) != (vmw_du_max + 1));
	drm_info(&dev_priv->drm, "%s display unit initialized\n",
		 display_unit_names[dev_priv->active_display_unit]);

	return ret;
}

int vmw_kms_close(struct vmw_private *dev_priv)
{
	int ret = 0;

	/*
	 * Docs says we should take the lock before calling this function
	 * but since it destroys encoders and our destructor calls
	 * drm_encoder_cleanup which takes the lock we deadlock.
	 */
	drm_mode_config_cleanup(&dev_priv->drm);
	if (dev_priv->active_display_unit == vmw_du_legacy)
		ret = vmw_kms_ldu_close_display(dev_priv);

	return ret;
}

int vmw_kms_write_svga(struct vmw_private *vmw_priv,
			unsigned width, unsigned height, unsigned pitch,
			unsigned bpp, unsigned depth)
{
	if (vmw_priv->capabilities & SVGA_CAP_PITCHLOCK)
		vmw_write(vmw_priv, SVGA_REG_PITCHLOCK, pitch);
	else if (vmw_fifo_have_pitchlock(vmw_priv))
		vmw_fifo_mem_write(vmw_priv, SVGA_FIFO_PITCHLOCK, pitch);
	vmw_write(vmw_priv, SVGA_REG_WIDTH, width);
	vmw_write(vmw_priv, SVGA_REG_HEIGHT, height);
	if ((vmw_priv->capabilities & SVGA_CAP_8BIT_EMULATION) != 0)
		vmw_write(vmw_priv, SVGA_REG_BITS_PER_PIXEL, bpp);

	if (vmw_read(vmw_priv, SVGA_REG_DEPTH) != depth) {
		DRM_ERROR("Invalid depth %u for %u bpp, host expects %u\n",
			  depth, bpp, vmw_read(vmw_priv, SVGA_REG_DEPTH));
		return -EINVAL;
	}

	return 0;
}

static
bool vmw_kms_validate_mode_vram(struct vmw_private *dev_priv,
				u64 pitch,
				u64 height)
{
	return (pitch * height) < (u64)dev_priv->vram_size;
}

/**
 * vmw_du_update_layout - Update the display unit with topology from resolution
 * plugin and generate DRM uevent
 * @dev_priv: device private
 * @num_rects: number of drm_rect in rects
 * @rects: toplogy to update
 */
static int vmw_du_update_layout(struct vmw_private *dev_priv,
				unsigned int num_rects, struct drm_rect *rects)
{
	struct drm_device *dev = &dev_priv->drm;
	struct vmw_display_unit *du;
	struct drm_connector *con;
	struct drm_connector_list_iter conn_iter;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_crtc *crtc;
	int ret;

	/* Currently gui_x/y is protected with the crtc mutex */
	mutex_lock(&dev->mode_config.mutex);
	drm_modeset_acquire_init(&ctx, 0);
retry:
	drm_for_each_crtc(crtc, dev) {
		ret = drm_modeset_lock(&crtc->mutex, &ctx);
		if (ret < 0) {
			if (ret == -EDEADLK) {
				drm_modeset_backoff(&ctx);
				goto retry;
		}
			goto out_fini;
		}
	}

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(con, &conn_iter) {
		du = vmw_connector_to_du(con);
		if (num_rects > du->unit) {
			du->pref_width = drm_rect_width(&rects[du->unit]);
			du->pref_height = drm_rect_height(&rects[du->unit]);
			du->pref_active = true;
			du->gui_x = rects[du->unit].x1;
			du->gui_y = rects[du->unit].y1;
		} else {
			du->pref_width  = VMWGFX_MIN_INITIAL_WIDTH;
			du->pref_height = VMWGFX_MIN_INITIAL_HEIGHT;
			du->pref_active = false;
			du->gui_x = 0;
			du->gui_y = 0;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	list_for_each_entry(con, &dev->mode_config.connector_list, head) {
		du = vmw_connector_to_du(con);
		if (num_rects > du->unit) {
			drm_object_property_set_value
			  (&con->base, dev->mode_config.suggested_x_property,
			   du->gui_x);
			drm_object_property_set_value
			  (&con->base, dev->mode_config.suggested_y_property,
			   du->gui_y);
		} else {
			drm_object_property_set_value
			  (&con->base, dev->mode_config.suggested_x_property,
			   0);
			drm_object_property_set_value
			  (&con->base, dev->mode_config.suggested_y_property,
			   0);
		}
		con->status = vmw_du_connector_detect(con, true);
	}
out_fini:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	mutex_unlock(&dev->mode_config.mutex);

	drm_sysfs_hotplug_event(dev);

	return 0;
}

int vmw_du_crtc_gamma_set(struct drm_crtc *crtc,
			  u16 *r, u16 *g, u16 *b,
			  uint32_t size,
			  struct drm_modeset_acquire_ctx *ctx)
{
	struct vmw_private *dev_priv = vmw_priv(crtc->dev);
	int i;

	for (i = 0; i < size; i++) {
		DRM_DEBUG("%d r/g/b = 0x%04x / 0x%04x / 0x%04x\n", i,
			  r[i], g[i], b[i]);
		vmw_write(dev_priv, SVGA_PALETTE_BASE + i * 3 + 0, r[i] >> 8);
		vmw_write(dev_priv, SVGA_PALETTE_BASE + i * 3 + 1, g[i] >> 8);
		vmw_write(dev_priv, SVGA_PALETTE_BASE + i * 3 + 2, b[i] >> 8);
	}

	return 0;
}

int vmw_du_connector_dpms(struct drm_connector *connector, int mode)
{
	return 0;
}

enum drm_connector_status
vmw_du_connector_detect(struct drm_connector *connector, bool force)
{
	uint32_t num_displays;
	struct drm_device *dev = connector->dev;
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_display_unit *du = vmw_connector_to_du(connector);

	num_displays = vmw_read(dev_priv, SVGA_REG_NUM_DISPLAYS);

	return ((vmw_connector_to_du(connector)->unit < num_displays &&
		 du->pref_active) ?
		connector_status_connected : connector_status_disconnected);
}

/**
 * vmw_guess_mode_timing - Provide fake timings for a
 * 60Hz vrefresh mode.
 *
 * @mode: Pointer to a struct drm_display_mode with hdisplay and vdisplay
 * members filled in.
 */
void vmw_guess_mode_timing(struct drm_display_mode *mode)
{
	mode->hsync_start = mode->hdisplay + 50;
	mode->hsync_end = mode->hsync_start + 50;
	mode->htotal = mode->hsync_end + 50;

	mode->vsync_start = mode->vdisplay + 50;
	mode->vsync_end = mode->vsync_start + 50;
	mode->vtotal = mode->vsync_end + 50;

	mode->clock = (u32)mode->htotal * (u32)mode->vtotal / 100 * 6;
}


/**
 * vmw_kms_update_layout_ioctl - Handler for DRM_VMW_UPDATE_LAYOUT ioctl
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Update preferred topology of display unit as per ioctl request. The topology
 * is expressed as array of drm_vmw_rect.
 * e.g.
 * [0 0 640 480] [640 0 800 600] [0 480 640 480]
 *
 * NOTE:
 * The x and y offset (upper left) in drm_vmw_rect cannot be less than 0. Beside
 * device limit on topology, x + w and y + h (lower right) cannot be greater
 * than INT_MAX. So topology beyond these limits will return with error.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int vmw_kms_update_layout_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_vmw_update_layout_arg *arg =
		(struct drm_vmw_update_layout_arg *)data;
	const void __user *user_rects;
	struct drm_vmw_rect *rects;
	struct drm_rect *drm_rects;
	unsigned rects_size;
	int ret, i;

	if (!arg->num_outputs) {
		struct drm_rect def_rect = {0, 0,
					    VMWGFX_MIN_INITIAL_WIDTH,
					    VMWGFX_MIN_INITIAL_HEIGHT};
		vmw_du_update_layout(dev_priv, 1, &def_rect);
		return 0;
	} else if (arg->num_outputs > VMWGFX_NUM_DISPLAY_UNITS) {
		return -E2BIG;
	}

	rects_size = arg->num_outputs * sizeof(struct drm_vmw_rect);
	rects = kcalloc(arg->num_outputs, sizeof(struct drm_vmw_rect),
			GFP_KERNEL);
	if (unlikely(!rects))
		return -ENOMEM;

	user_rects = (void __user *)(unsigned long)arg->rects;
	ret = copy_from_user(rects, user_rects, rects_size);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed to get rects.\n");
		ret = -EFAULT;
		goto out_free;
	}

	drm_rects = (struct drm_rect *)rects;

	VMW_DEBUG_KMS("Layout count = %u\n", arg->num_outputs);
	for (i = 0; i < arg->num_outputs; i++) {
		struct drm_vmw_rect curr_rect;

		/* Verify user-space for overflow as kernel use drm_rect */
		if ((rects[i].x + rects[i].w > INT_MAX) ||
		    (rects[i].y + rects[i].h > INT_MAX)) {
			ret = -ERANGE;
			goto out_free;
		}

		curr_rect = rects[i];
		drm_rects[i].x1 = curr_rect.x;
		drm_rects[i].y1 = curr_rect.y;
		drm_rects[i].x2 = curr_rect.x + curr_rect.w;
		drm_rects[i].y2 = curr_rect.y + curr_rect.h;

		VMW_DEBUG_KMS("  x1 = %d y1 = %d x2 = %d y2 = %d\n",
			      drm_rects[i].x1, drm_rects[i].y1,
			      drm_rects[i].x2, drm_rects[i].y2);

		/*
		 * Currently this check is limiting the topology within
		 * mode_config->max (which actually is max texture size
		 * supported by virtual device). This limit is here to address
		 * window managers that create a big framebuffer for whole
		 * topology.
		 */
		if (drm_rects[i].x1 < 0 ||  drm_rects[i].y1 < 0 ||
		    drm_rects[i].x2 > mode_config->max_width ||
		    drm_rects[i].y2 > mode_config->max_height) {
			VMW_DEBUG_KMS("Invalid layout %d %d %d %d\n",
				      drm_rects[i].x1, drm_rects[i].y1,
				      drm_rects[i].x2, drm_rects[i].y2);
			ret = -EINVAL;
			goto out_free;
		}
	}

	ret = vmw_kms_check_display_memory(dev, arg->num_outputs, drm_rects);

	if (ret == 0)
		vmw_du_update_layout(dev_priv, arg->num_outputs, drm_rects);

out_free:
	kfree(rects);
	return ret;
}

/**
 * vmw_kms_helper_dirty - Helper to build commands and perform actions based
 * on a set of cliprects and a set of display units.
 *
 * @dev_priv: Pointer to a device private structure.
 * @framebuffer: Pointer to the framebuffer on which to perform the actions.
 * @clips: A set of struct drm_clip_rect. Either this os @vclips must be NULL.
 * Cliprects are given in framebuffer coordinates.
 * @vclips: A set of struct drm_vmw_rect cliprects. Either this or @clips must
 * be NULL. Cliprects are given in source coordinates.
 * @dest_x: X coordinate offset for the crtc / destination clip rects.
 * @dest_y: Y coordinate offset for the crtc / destination clip rects.
 * @num_clips: Number of cliprects in the @clips or @vclips array.
 * @increment: Integer with which to increment the clip counter when looping.
 * Used to skip a predetermined number of clip rects.
 * @dirty: Closure structure. See the description of struct vmw_kms_dirty.
 */
int vmw_kms_helper_dirty(struct vmw_private *dev_priv,
			 struct vmw_framebuffer *framebuffer,
			 const struct drm_clip_rect *clips,
			 const struct drm_vmw_rect *vclips,
			 s32 dest_x, s32 dest_y,
			 int num_clips,
			 int increment,
			 struct vmw_kms_dirty *dirty)
{
	struct vmw_display_unit *units[VMWGFX_NUM_DISPLAY_UNITS];
	struct drm_crtc *crtc;
	u32 num_units = 0;
	u32 i, k;

	dirty->dev_priv = dev_priv;

	/* If crtc is passed, no need to iterate over other display units */
	if (dirty->crtc) {
		units[num_units++] = vmw_crtc_to_du(dirty->crtc);
	} else {
		list_for_each_entry(crtc, &dev_priv->drm.mode_config.crtc_list,
				    head) {
			struct drm_plane *plane = crtc->primary;

			if (plane->state->fb == &framebuffer->base)
				units[num_units++] = vmw_crtc_to_du(crtc);
		}
	}

	for (k = 0; k < num_units; k++) {
		struct vmw_display_unit *unit = units[k];
		s32 crtc_x = unit->crtc.x;
		s32 crtc_y = unit->crtc.y;
		s32 crtc_width = unit->crtc.mode.hdisplay;
		s32 crtc_height = unit->crtc.mode.vdisplay;
		const struct drm_clip_rect *clips_ptr = clips;
		const struct drm_vmw_rect *vclips_ptr = vclips;

		dirty->unit = unit;
		if (dirty->fifo_reserve_size > 0) {
			dirty->cmd = VMW_CMD_RESERVE(dev_priv,
						      dirty->fifo_reserve_size);
			if (!dirty->cmd)
				return -ENOMEM;

			memset(dirty->cmd, 0, dirty->fifo_reserve_size);
		}
		dirty->num_hits = 0;
		for (i = 0; i < num_clips; i++, clips_ptr += increment,
		       vclips_ptr += increment) {
			s32 clip_left;
			s32 clip_top;

			/*
			 * Select clip array type. Note that integer type
			 * in @clips is unsigned short, whereas in @vclips
			 * it's 32-bit.
			 */
			if (clips) {
				dirty->fb_x = (s32) clips_ptr->x1;
				dirty->fb_y = (s32) clips_ptr->y1;
				dirty->unit_x2 = (s32) clips_ptr->x2 + dest_x -
					crtc_x;
				dirty->unit_y2 = (s32) clips_ptr->y2 + dest_y -
					crtc_y;
			} else {
				dirty->fb_x = vclips_ptr->x;
				dirty->fb_y = vclips_ptr->y;
				dirty->unit_x2 = dirty->fb_x + vclips_ptr->w +
					dest_x - crtc_x;
				dirty->unit_y2 = dirty->fb_y + vclips_ptr->h +
					dest_y - crtc_y;
			}

			dirty->unit_x1 = dirty->fb_x + dest_x - crtc_x;
			dirty->unit_y1 = dirty->fb_y + dest_y - crtc_y;

			/* Skip this clip if it's outside the crtc region */
			if (dirty->unit_x1 >= crtc_width ||
			    dirty->unit_y1 >= crtc_height ||
			    dirty->unit_x2 <= 0 || dirty->unit_y2 <= 0)
				continue;

			/* Clip right and bottom to crtc limits */
			dirty->unit_x2 = min_t(s32, dirty->unit_x2,
					       crtc_width);
			dirty->unit_y2 = min_t(s32, dirty->unit_y2,
					       crtc_height);

			/* Clip left and top to crtc limits */
			clip_left = min_t(s32, dirty->unit_x1, 0);
			clip_top = min_t(s32, dirty->unit_y1, 0);
			dirty->unit_x1 -= clip_left;
			dirty->unit_y1 -= clip_top;
			dirty->fb_x -= clip_left;
			dirty->fb_y -= clip_top;

			dirty->clip(dirty);
		}

		dirty->fifo_commit(dirty);
	}

	return 0;
}

/**
 * vmw_kms_helper_validation_finish - Helper for post KMS command submission
 * cleanup and fencing
 * @dev_priv: Pointer to the device-private struct
 * @file_priv: Pointer identifying the client when user-space fencing is used
 * @ctx: Pointer to the validation context
 * @out_fence: If non-NULL, returned refcounted fence-pointer
 * @user_fence_rep: If non-NULL, pointer to user-space address area
 * in which to copy user-space fence info
 */
void vmw_kms_helper_validation_finish(struct vmw_private *dev_priv,
				      struct drm_file *file_priv,
				      struct vmw_validation_context *ctx,
				      struct vmw_fence_obj **out_fence,
				      struct drm_vmw_fence_rep __user *
				      user_fence_rep)
{
	struct vmw_fence_obj *fence = NULL;
	uint32_t handle = 0;
	int ret = 0;

	if (file_priv || user_fence_rep || vmw_validation_has_bos(ctx) ||
	    out_fence)
		ret = vmw_execbuf_fence_commands(file_priv, dev_priv, &fence,
						 file_priv ? &handle : NULL);
	vmw_validation_done(ctx, fence);
	if (file_priv)
		vmw_execbuf_copy_fence_user(dev_priv, vmw_fpriv(file_priv),
					    ret, user_fence_rep, fence,
					    handle, -1);
	if (out_fence)
		*out_fence = fence;
	else
		vmw_fence_obj_unreference(&fence);
}

/**
 * vmw_kms_create_implicit_placement_property - Set up the implicit placement
 * property.
 *
 * @dev_priv: Pointer to a device private struct.
 *
 * Sets up the implicit placement property unless it's already set up.
 */
void
vmw_kms_create_implicit_placement_property(struct vmw_private *dev_priv)
{
	if (dev_priv->implicit_placement_property)
		return;

	dev_priv->implicit_placement_property =
		drm_property_create_range(&dev_priv->drm,
					  DRM_MODE_PROP_IMMUTABLE,
					  "implicit_placement", 0, 1);
}

/**
 * vmw_kms_suspend - Save modesetting state and turn modesetting off.
 *
 * @dev: Pointer to the drm device
 * Return: 0 on success. Negative error code on failure.
 */
int vmw_kms_suspend(struct drm_device *dev)
{
	struct vmw_private *dev_priv = vmw_priv(dev);

	dev_priv->suspend_state = drm_atomic_helper_suspend(dev);
	if (IS_ERR(dev_priv->suspend_state)) {
		int ret = PTR_ERR(dev_priv->suspend_state);

		DRM_ERROR("Failed kms suspend: %d\n", ret);
		dev_priv->suspend_state = NULL;

		return ret;
	}

	return 0;
}


/**
 * vmw_kms_resume - Re-enable modesetting and restore state
 *
 * @dev: Pointer to the drm device
 * Return: 0 on success. Negative error code on failure.
 *
 * State is resumed from a previous vmw_kms_suspend(). It's illegal
 * to call this function without a previous vmw_kms_suspend().
 */
int vmw_kms_resume(struct drm_device *dev)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	int ret;

	if (WARN_ON(!dev_priv->suspend_state))
		return 0;

	ret = drm_atomic_helper_resume(dev, dev_priv->suspend_state);
	dev_priv->suspend_state = NULL;

	return ret;
}

/**
 * vmw_kms_lost_device - Notify kms that modesetting capabilities will be lost
 *
 * @dev: Pointer to the drm device
 */
void vmw_kms_lost_device(struct drm_device *dev)
{
	drm_atomic_helper_shutdown(dev);
}

/**
 * vmw_du_helper_plane_update - Helper to do plane update on a display unit.
 * @update: The closure structure.
 *
 * Call this helper after setting callbacks in &vmw_du_update_plane to do plane
 * update on display unit.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int vmw_du_helper_plane_update(struct vmw_du_update_plane *update)
{
	struct drm_plane_state *state = update->plane->state;
	struct drm_plane_state *old_state = update->old_state;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect clip;
	struct drm_rect bb;
	DECLARE_VAL_CONTEXT(val_ctx, NULL, 0);
	uint32_t reserved_size = 0;
	uint32_t submit_size = 0;
	uint32_t curr_size = 0;
	uint32_t num_hits = 0;
	void *cmd_start;
	char *cmd_next;
	int ret;

	/*
	 * Iterate in advance to check if really need plane update and find the
	 * number of clips that actually are in plane src for fifo allocation.
	 */
	drm_atomic_helper_damage_iter_init(&iter, old_state, state);
	drm_atomic_for_each_plane_damage(&iter, &clip)
		num_hits++;

	if (num_hits == 0)
		return 0;

	if (update->vfb->bo) {
		struct vmw_framebuffer_bo *vfbbo =
			container_of(update->vfb, typeof(*vfbbo), base);

		/*
		 * For screen targets we want a mappable bo, for everything else we want
		 * accelerated i.e. host backed (vram or gmr) bo. If the display unit
		 * is not screen target then mob's shouldn't be available.
		 */
		if (update->dev_priv->active_display_unit == vmw_du_screen_target) {
			vmw_bo_placement_set(vfbbo->buffer,
					     VMW_BO_DOMAIN_SYS | VMW_BO_DOMAIN_MOB | VMW_BO_DOMAIN_GMR,
					     VMW_BO_DOMAIN_SYS | VMW_BO_DOMAIN_MOB | VMW_BO_DOMAIN_GMR);
		} else {
			WARN_ON(update->dev_priv->has_mob);
			vmw_bo_placement_set_default_accelerated(vfbbo->buffer);
		}
		ret = vmw_validation_add_bo(&val_ctx, vfbbo->buffer);
	} else {
		struct vmw_framebuffer_surface *vfbs =
			container_of(update->vfb, typeof(*vfbs), base);
		struct vmw_surface *surf = vmw_user_object_surface(&vfbs->uo);

		ret = vmw_validation_add_resource(&val_ctx, &surf->res,
						  0, VMW_RES_DIRTY_NONE, NULL,
						  NULL);
	}

	if (ret)
		return ret;

	ret = vmw_validation_prepare(&val_ctx, update->mutex, update->intr);
	if (ret)
		goto out_unref;

	reserved_size = update->calc_fifo_size(update, num_hits);
	cmd_start = VMW_CMD_RESERVE(update->dev_priv, reserved_size);
	if (!cmd_start) {
		ret = -ENOMEM;
		goto out_revert;
	}

	cmd_next = cmd_start;

	if (update->post_prepare) {
		curr_size = update->post_prepare(update, cmd_next);
		cmd_next += curr_size;
		submit_size += curr_size;
	}

	if (update->pre_clip) {
		curr_size = update->pre_clip(update, cmd_next, num_hits);
		cmd_next += curr_size;
		submit_size += curr_size;
	}

	bb.x1 = INT_MAX;
	bb.y1 = INT_MAX;
	bb.x2 = INT_MIN;
	bb.y2 = INT_MIN;

	drm_atomic_helper_damage_iter_init(&iter, old_state, state);
	drm_atomic_for_each_plane_damage(&iter, &clip) {
		uint32_t fb_x = clip.x1;
		uint32_t fb_y = clip.y1;

		vmw_du_translate_to_crtc(state, &clip);
		if (update->clip) {
			curr_size = update->clip(update, cmd_next, &clip, fb_x,
						 fb_y);
			cmd_next += curr_size;
			submit_size += curr_size;
		}
		bb.x1 = min_t(int, bb.x1, clip.x1);
		bb.y1 = min_t(int, bb.y1, clip.y1);
		bb.x2 = max_t(int, bb.x2, clip.x2);
		bb.y2 = max_t(int, bb.y2, clip.y2);
	}

	curr_size = update->post_clip(update, cmd_next, &bb);
	submit_size += curr_size;

	if (reserved_size < submit_size)
		submit_size = 0;

	vmw_cmd_commit(update->dev_priv, submit_size);

	vmw_kms_helper_validation_finish(update->dev_priv, NULL, &val_ctx,
					 update->out_fence, NULL);
	return ret;

out_revert:
	vmw_validation_revert(&val_ctx);

out_unref:
	vmw_validation_unref_lists(&val_ctx);
	return ret;
}

/**
 * vmw_connector_mode_valid - implements drm_connector_helper_funcs.mode_valid callback
 *
 * @connector: the drm connector, part of a DU container
 * @mode: drm mode to check
 *
 * Returns MODE_OK on success, or a drm_mode_status error code.
 */
enum drm_mode_status vmw_connector_mode_valid(struct drm_connector *connector,
					      const struct drm_display_mode *mode)
{
	enum drm_mode_status ret;
	struct drm_device *dev = connector->dev;
	struct vmw_private *dev_priv = vmw_priv(dev);
	u32 assumed_cpp = 4;

	if (dev_priv->assume_16bpp)
		assumed_cpp = 2;

	ret = drm_mode_validate_size(mode, dev_priv->texture_max_width,
				     dev_priv->texture_max_height);
	if (ret != MODE_OK)
		return ret;

	if (!vmw_kms_validate_mode_vram(dev_priv,
					mode->hdisplay * assumed_cpp,
					mode->vdisplay))
		return MODE_MEM;

	return MODE_OK;
}

/**
 * vmw_connector_get_modes - implements drm_connector_helper_funcs.get_modes callback
 *
 * @connector: the drm connector, part of a DU container
 *
 * Returns the number of added modes.
 */
int vmw_connector_get_modes(struct drm_connector *connector)
{
	struct vmw_display_unit *du = vmw_connector_to_du(connector);
	struct drm_device *dev = connector->dev;
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct drm_display_mode *mode = NULL;
	struct drm_display_mode prefmode = { DRM_MODE("preferred",
		DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC)
	};
	u32 max_width;
	u32 max_height;
	u32 num_modes;

	/* Add preferred mode */
	mode = drm_mode_duplicate(dev, &prefmode);
	if (!mode)
		return 0;

	mode->hdisplay = du->pref_width;
	mode->vdisplay = du->pref_height;
	vmw_guess_mode_timing(mode);
	drm_mode_set_name(mode);

	drm_mode_probed_add(connector, mode);
	drm_dbg_kms(dev, "preferred mode " DRM_MODE_FMT "\n", DRM_MODE_ARG(mode));

	/* Probe connector for all modes not exceeding our geom limits */
	max_width  = dev_priv->texture_max_width;
	max_height = dev_priv->texture_max_height;

	if (dev_priv->active_display_unit == vmw_du_screen_target) {
		max_width  = min(dev_priv->stdu_max_width,  max_width);
		max_height = min(dev_priv->stdu_max_height, max_height);
	}

	num_modes = 1 + drm_add_modes_noedid(connector, max_width, max_height);

	return num_modes;
}

struct vmw_user_object *vmw_user_object_ref(struct vmw_user_object *uo)
{
	if (uo->buffer)
		vmw_user_bo_ref(uo->buffer);
	else if (uo->surface)
		vmw_surface_reference(uo->surface);
	return uo;
}

void vmw_user_object_unref(struct vmw_user_object *uo)
{
	if (uo->buffer)
		vmw_user_bo_unref(&uo->buffer);
	else if (uo->surface)
		vmw_surface_unreference(&uo->surface);
}

struct vmw_bo *
vmw_user_object_buffer(struct vmw_user_object *uo)
{
	if (uo->buffer)
		return uo->buffer;
	else if (uo->surface)
		return uo->surface->res.guest_memory_bo;
	return NULL;
}

struct vmw_surface *
vmw_user_object_surface(struct vmw_user_object *uo)
{
	if (uo->buffer)
		return uo->buffer->dumb_surface;
	return uo->surface;
}

void *vmw_user_object_map(struct vmw_user_object *uo)
{
	struct vmw_bo *bo = vmw_user_object_buffer(uo);

	WARN_ON(!bo);
	return vmw_bo_map_and_cache(bo);
}

void *vmw_user_object_map_size(struct vmw_user_object *uo, size_t size)
{
	struct vmw_bo *bo = vmw_user_object_buffer(uo);

	WARN_ON(!bo);
	return vmw_bo_map_and_cache_size(bo, size);
}

void vmw_user_object_unmap(struct vmw_user_object *uo)
{
	struct vmw_bo *bo = vmw_user_object_buffer(uo);
	int ret;

	WARN_ON(!bo);

	/* Fence the mob creation so we are guarateed to have the mob */
	ret = ttm_bo_reserve(&bo->tbo, false, false, NULL);
	if (ret != 0)
		return;

	vmw_bo_unmap(bo);
	vmw_bo_pin_reserved(bo, false);

	ttm_bo_unreserve(&bo->tbo);
}

bool vmw_user_object_is_mapped(struct vmw_user_object *uo)
{
	struct vmw_bo *bo;

	if (!uo || vmw_user_object_is_null(uo))
		return false;

	bo = vmw_user_object_buffer(uo);

	if (WARN_ON(!bo))
		return false;

	WARN_ON(bo->map.bo && !bo->map.virtual);
	return bo->map.virtual;
}

bool vmw_user_object_is_null(struct vmw_user_object *uo)
{
	return !uo->buffer && !uo->surface;
}
