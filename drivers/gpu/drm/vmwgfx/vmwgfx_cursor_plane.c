// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright (c) 2024-2025 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 *
 **************************************************************************/
#include "vmwgfx_cursor_plane.h"

#include "vmwgfx_bo.h"
#include "vmwgfx_drv.h"
#include "vmwgfx_kms.h"
#include "vmwgfx_resource_priv.h"
#include "vmw_surface_cache.h"

#include "drm/drm_atomic.h"
#include "drm/drm_atomic_helper.h"
#include "drm/drm_plane.h"
#include <asm/page.h>

#define VMW_CURSOR_SNOOP_FORMAT SVGA3D_A8R8G8B8
#define VMW_CURSOR_SNOOP_WIDTH 64
#define VMW_CURSOR_SNOOP_HEIGHT 64

struct vmw_svga_fifo_cmd_define_cursor {
	u32 cmd;
	SVGAFifoCmdDefineAlphaCursor cursor;
};

/**
 * vmw_send_define_cursor_cmd - queue a define cursor command
 * @dev_priv: the private driver struct
 * @image: buffer which holds the cursor image
 * @width: width of the mouse cursor image
 * @height: height of the mouse cursor image
 * @hotspotX: the horizontal position of mouse hotspot
 * @hotspotY: the vertical position of mouse hotspot
 */
static void vmw_send_define_cursor_cmd(struct vmw_private *dev_priv,
				       u32 *image, u32 width, u32 height,
				       u32 hotspotX, u32 hotspotY)
{
	struct vmw_svga_fifo_cmd_define_cursor *cmd;
	const u32 image_size = width * height * sizeof(*image);
	const u32 cmd_size = sizeof(*cmd) + image_size;

	/*
	 * Try to reserve fifocmd space and swallow any failures;
	 * such reservations cannot be left unconsumed for long
	 * under the risk of clogging other fifocmd users, so
	 * we treat reservations separtely from the way we treat
	 * other fallible KMS-atomic resources at prepare_fb
	 */
	cmd = VMW_CMD_RESERVE(dev_priv, cmd_size);

	if (unlikely(!cmd))
		return;

	memset(cmd, 0, sizeof(*cmd));

	memcpy(&cmd[1], image, image_size);

	cmd->cmd = SVGA_CMD_DEFINE_ALPHA_CURSOR;
	cmd->cursor.id = 0;
	cmd->cursor.width = width;
	cmd->cursor.height = height;
	cmd->cursor.hotspotX = hotspotX;
	cmd->cursor.hotspotY = hotspotY;

	vmw_cmd_commit_flush(dev_priv, cmd_size);
}

static void
vmw_cursor_plane_update_legacy(struct vmw_private *vmw,
			       struct vmw_plane_state *vps)
{
	struct vmw_surface *surface = vmw_user_object_surface(&vps->uo);
	s32 hotspot_x = vps->cursor.legacy.hotspot_x + vps->base.hotspot_x;
	s32 hotspot_y = vps->cursor.legacy.hotspot_y + vps->base.hotspot_y;

	if (WARN_ON(!surface || !surface->snooper.image))
		return;

	if (vps->cursor.legacy.id != surface->snooper.id) {
		vmw_send_define_cursor_cmd(vmw, surface->snooper.image,
					   vps->base.crtc_w, vps->base.crtc_h,
					   hotspot_x, hotspot_y);
		vps->cursor.legacy.id = surface->snooper.id;
	}
}

static enum vmw_cursor_update_type
vmw_cursor_update_type(struct vmw_private *vmw, struct vmw_plane_state *vps)
{
	struct vmw_surface *surface = vmw_user_object_surface(&vps->uo);

	if (surface && surface->snooper.image)
		return VMW_CURSOR_UPDATE_LEGACY;

	if (vmw->has_mob) {
		if ((vmw->capabilities2 & SVGA_CAP2_CURSOR_MOB) != 0)
			return VMW_CURSOR_UPDATE_MOB;
		else
			return VMW_CURSOR_UPDATE_GB_ONLY;
	}
	drm_warn_once(&vmw->drm, "Unknown Cursor Type!\n");
	return VMW_CURSOR_UPDATE_NONE;
}

static void vmw_cursor_update_mob(struct vmw_private *vmw,
				  struct vmw_plane_state *vps)
{
	SVGAGBCursorHeader *header;
	SVGAGBAlphaCursorHeader *alpha_header;
	struct vmw_bo *bo = vmw_user_object_buffer(&vps->uo);
	u32 *image = vmw_bo_map_and_cache(bo);
	const u32 image_size = vps->base.crtc_w * vps->base.crtc_h * sizeof(*image);

	header = vmw_bo_map_and_cache(vps->cursor.mob);
	alpha_header = &header->header.alphaHeader;

	memset(header, 0, sizeof(*header));

	header->type = SVGA_ALPHA_CURSOR;
	header->sizeInBytes = image_size;

	alpha_header->hotspotX = vps->cursor.legacy.hotspot_x + vps->base.hotspot_x;
	alpha_header->hotspotY = vps->cursor.legacy.hotspot_y + vps->base.hotspot_y;
	alpha_header->width = vps->base.crtc_w;
	alpha_header->height = vps->base.crtc_h;

	memcpy(header + 1, image, image_size);
	vmw_write(vmw, SVGA_REG_CURSOR_MOBID, vmw_bo_mobid(vps->cursor.mob));

	vmw_bo_unmap(bo);
	vmw_bo_unmap(vps->cursor.mob);
}

static u32 vmw_cursor_mob_size(enum vmw_cursor_update_type update_type,
			       u32 w, u32 h)
{
	switch (update_type) {
	case VMW_CURSOR_UPDATE_LEGACY:
	case VMW_CURSOR_UPDATE_GB_ONLY:
	case VMW_CURSOR_UPDATE_NONE:
		return 0;
	case VMW_CURSOR_UPDATE_MOB:
		return w * h * sizeof(u32) + sizeof(SVGAGBCursorHeader);
	}
	return 0;
}

static void vmw_cursor_mob_destroy(struct vmw_bo **vbo)
{
	if (!(*vbo))
		return;

	ttm_bo_unpin(&(*vbo)->tbo);
	vmw_bo_unreference(vbo);
}

/**
 * vmw_cursor_mob_unmap - Unmaps the cursor mobs.
 *
 * @vps: state of the cursor plane
 *
 * Returns 0 on success
 */

static int
vmw_cursor_mob_unmap(struct vmw_plane_state *vps)
{
	int ret = 0;
	struct vmw_bo *vbo = vps->cursor.mob;

	if (!vbo || !vbo->map.virtual)
		return 0;

	ret = ttm_bo_reserve(&vbo->tbo, true, false, NULL);
	if (likely(ret == 0)) {
		vmw_bo_unmap(vbo);
		ttm_bo_unreserve(&vbo->tbo);
	}

	return ret;
}

static void vmw_cursor_mob_put(struct vmw_cursor_plane *vcp,
			       struct vmw_plane_state *vps)
{
	u32 i;

	if (!vps->cursor.mob)
		return;

	vmw_cursor_mob_unmap(vps);

	/* Look for a free slot to return this mob to the cache. */
	for (i = 0; i < ARRAY_SIZE(vcp->cursor_mobs); i++) {
		if (!vcp->cursor_mobs[i]) {
			vcp->cursor_mobs[i] = vps->cursor.mob;
			vps->cursor.mob = NULL;
			return;
		}
	}

	/* Cache is full: See if this mob is bigger than an existing mob. */
	for (i = 0; i < ARRAY_SIZE(vcp->cursor_mobs); i++) {
		if (vcp->cursor_mobs[i]->tbo.base.size <
		    vps->cursor.mob->tbo.base.size) {
			vmw_cursor_mob_destroy(&vcp->cursor_mobs[i]);
			vcp->cursor_mobs[i] = vps->cursor.mob;
			vps->cursor.mob = NULL;
			return;
		}
	}

	/* Destroy it if it's not worth caching. */
	vmw_cursor_mob_destroy(&vps->cursor.mob);
}

static int vmw_cursor_mob_get(struct vmw_cursor_plane *vcp,
			      struct vmw_plane_state *vps)
{
	struct vmw_private *dev_priv = vmw_priv(vcp->base.dev);
	u32 size = vmw_cursor_mob_size(vps->cursor.update_type,
				       vps->base.crtc_w, vps->base.crtc_h);
	u32 i;
	u32 cursor_max_dim, mob_max_size;
	struct vmw_fence_obj *fence = NULL;
	int ret;

	if (!dev_priv->has_mob ||
	    (dev_priv->capabilities2 & SVGA_CAP2_CURSOR_MOB) == 0)
		return -EINVAL;

	mob_max_size = vmw_read(dev_priv, SVGA_REG_MOB_MAX_SIZE);
	cursor_max_dim = vmw_read(dev_priv, SVGA_REG_CURSOR_MAX_DIMENSION);

	if (size > mob_max_size || vps->base.crtc_w > cursor_max_dim ||
	    vps->base.crtc_h > cursor_max_dim)
		return -EINVAL;

	if (vps->cursor.mob) {
		if (vps->cursor.mob->tbo.base.size >= size)
			return 0;
		vmw_cursor_mob_put(vcp, vps);
	}

	/* Look for an unused mob in the cache. */
	for (i = 0; i < ARRAY_SIZE(vcp->cursor_mobs); i++) {
		if (vcp->cursor_mobs[i] &&
		    vcp->cursor_mobs[i]->tbo.base.size >= size) {
			vps->cursor.mob = vcp->cursor_mobs[i];
			vcp->cursor_mobs[i] = NULL;
			return 0;
		}
	}
	/* Create a new mob if we can't find an existing one. */
	ret = vmw_bo_create_and_populate(dev_priv, size, VMW_BO_DOMAIN_MOB,
					 &vps->cursor.mob);

	if (ret != 0)
		return ret;

	/* Fence the mob creation so we are guarateed to have the mob */
	ret = ttm_bo_reserve(&vps->cursor.mob->tbo, false, false, NULL);
	if (ret != 0)
		goto teardown;

	ret = vmw_execbuf_fence_commands(NULL, dev_priv, &fence, NULL);
	if (ret != 0) {
		ttm_bo_unreserve(&vps->cursor.mob->tbo);
		goto teardown;
	}

	dma_fence_wait(&fence->base, false);
	dma_fence_put(&fence->base);

	ttm_bo_unreserve(&vps->cursor.mob->tbo);

	return 0;

teardown:
	vmw_cursor_mob_destroy(&vps->cursor.mob);
	return ret;
}

static void vmw_cursor_update_position(struct vmw_private *dev_priv,
				       bool show, int x, int y)
{
	const u32 svga_cursor_on = show ? SVGA_CURSOR_ON_SHOW
				   : SVGA_CURSOR_ON_HIDE;
	u32 count;

	spin_lock(&dev_priv->cursor_lock);
	if (dev_priv->capabilities2 & SVGA_CAP2_EXTRA_REGS) {
		vmw_write(dev_priv, SVGA_REG_CURSOR4_X, x);
		vmw_write(dev_priv, SVGA_REG_CURSOR4_Y, y);
		vmw_write(dev_priv, SVGA_REG_CURSOR4_SCREEN_ID, SVGA3D_INVALID_ID);
		vmw_write(dev_priv, SVGA_REG_CURSOR4_ON, svga_cursor_on);
		vmw_write(dev_priv, SVGA_REG_CURSOR4_SUBMIT, 1);
	} else if (vmw_is_cursor_bypass3_enabled(dev_priv)) {
		vmw_fifo_mem_write(dev_priv, SVGA_FIFO_CURSOR_ON, svga_cursor_on);
		vmw_fifo_mem_write(dev_priv, SVGA_FIFO_CURSOR_X, x);
		vmw_fifo_mem_write(dev_priv, SVGA_FIFO_CURSOR_Y, y);
		count = vmw_fifo_mem_read(dev_priv, SVGA_FIFO_CURSOR_COUNT);
		vmw_fifo_mem_write(dev_priv, SVGA_FIFO_CURSOR_COUNT, ++count);
	} else {
		vmw_write(dev_priv, SVGA_REG_CURSOR_X, x);
		vmw_write(dev_priv, SVGA_REG_CURSOR_Y, y);
		vmw_write(dev_priv, SVGA_REG_CURSOR_ON, svga_cursor_on);
	}
	spin_unlock(&dev_priv->cursor_lock);
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
	u32 box_count;
	void *virtual;
	bool is_iomem;
	struct vmw_dma_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdSurfaceDMA dma;
	} *cmd;
	int i, ret;
	const struct SVGA3dSurfaceDesc *desc =
		vmw_surface_get_desc(VMW_CURSOR_SNOOP_FORMAT);
	const u32 image_pitch = VMW_CURSOR_SNOOP_WIDTH * desc->pitchBytesPerBlock;

	cmd = container_of(header, struct vmw_dma_cmd, header);

	/* No snooper installed, nothing to copy */
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

	if (cmd->dma.guest.ptr.offset % PAGE_SIZE ||
	    box->x != 0    || box->y != 0    || box->z != 0    ||
	    box->srcx != 0 || box->srcy != 0 || box->srcz != 0 ||
	    box->d != 1    || box_count != 1 ||
	    box->w > VMW_CURSOR_SNOOP_WIDTH || box->h > VMW_CURSOR_SNOOP_HEIGHT) {
		/* TODO handle none page aligned offsets */
		/* TODO handle more dst & src != 0 */
		/* TODO handle more then one copy */
		DRM_ERROR("Can't snoop dma request for cursor!\n");
		DRM_ERROR("(%u, %u, %u) (%u, %u, %u) (%ux%ux%u) %u %u\n",
			  box->srcx, box->srcy, box->srcz,
			  box->x, box->y, box->z,
			  box->w, box->h, box->d, box_count,
			  cmd->dma.guest.ptr.offset);
		return;
	}

	kmap_offset = cmd->dma.guest.ptr.offset >> PAGE_SHIFT;
	kmap_num = (VMW_CURSOR_SNOOP_HEIGHT * image_pitch) >> PAGE_SHIFT;

	ret = ttm_bo_reserve(bo, true, false, NULL);
	if (unlikely(ret != 0)) {
		DRM_ERROR("reserve failed\n");
		return;
	}

	ret = ttm_bo_kmap(bo, kmap_offset, kmap_num, &map);
	if (unlikely(ret != 0))
		goto err_unreserve;

	virtual = ttm_kmap_obj_virtual(&map, &is_iomem);

	if (box->w == VMW_CURSOR_SNOOP_WIDTH && cmd->dma.guest.pitch == image_pitch) {
		memcpy(srf->snooper.image, virtual,
		       VMW_CURSOR_SNOOP_HEIGHT * image_pitch);
	} else {
		/* Image is unsigned pointer. */
		for (i = 0; i < box->h; i++)
			memcpy(srf->snooper.image + i * image_pitch,
			       virtual + i * cmd->dma.guest.pitch,
			       box->w * desc->pitchBytesPerBlock);
	}
	srf->snooper.id++;

	ttm_bo_kunmap(&map);
err_unreserve:
	ttm_bo_unreserve(bo);
}

void vmw_cursor_plane_destroy(struct drm_plane *plane)
{
	struct vmw_cursor_plane *vcp = vmw_plane_to_vcp(plane);
	u32 i;

	vmw_cursor_update_position(vmw_priv(plane->dev), false, 0, 0);

	for (i = 0; i < ARRAY_SIZE(vcp->cursor_mobs); i++)
		vmw_cursor_mob_destroy(&vcp->cursor_mobs[i]);

	drm_plane_cleanup(plane);
}

/**
 * vmw_cursor_mob_map - Maps the cursor mobs.
 *
 * @vps: plane_state
 *
 * Returns 0 on success
 */

static int
vmw_cursor_mob_map(struct vmw_plane_state *vps)
{
	int ret;
	u32 size = vmw_cursor_mob_size(vps->cursor.update_type,
				       vps->base.crtc_w, vps->base.crtc_h);
	struct vmw_bo *vbo = vps->cursor.mob;

	if (!vbo)
		return -EINVAL;

	if (vbo->tbo.base.size < size)
		return -EINVAL;

	if (vbo->map.virtual)
		return 0;

	ret = ttm_bo_reserve(&vbo->tbo, false, false, NULL);
	if (unlikely(ret != 0))
		return -ENOMEM;

	vmw_bo_map_and_cache(vbo);

	ttm_bo_unreserve(&vbo->tbo);

	return 0;
}

/**
 * vmw_cursor_plane_cleanup_fb - Unpins the plane surface
 *
 * @plane: cursor plane
 * @old_state: contains the state to clean up
 *
 * Unmaps all cursor bo mappings and unpins the cursor surface
 *
 * Returns 0 on success
 */
void
vmw_cursor_plane_cleanup_fb(struct drm_plane *plane,
			    struct drm_plane_state *old_state)
{
	struct vmw_cursor_plane *vcp = vmw_plane_to_vcp(plane);
	struct vmw_plane_state *vps = vmw_plane_state_to_vps(old_state);

	if (!vmw_user_object_is_null(&vps->uo))
		vmw_user_object_unmap(&vps->uo);

	vmw_cursor_mob_unmap(vps);
	vmw_cursor_mob_put(vcp, vps);

	vmw_du_plane_unpin_surf(vps);
	vmw_user_object_unref(&vps->uo);
}

static bool
vmw_cursor_buffer_changed(struct vmw_plane_state *new_vps,
			  struct vmw_plane_state *old_vps)
{
	struct vmw_bo *new_bo = vmw_user_object_buffer(&new_vps->uo);
	struct vmw_bo *old_bo = vmw_user_object_buffer(&old_vps->uo);
	struct vmw_surface *surf;
	bool dirty = false;
	int ret;

	if (new_bo != old_bo)
		return true;

	if (new_bo) {
		if (!old_bo) {
			return true;
		} else if (new_bo->dirty) {
			vmw_bo_dirty_scan(new_bo);
			dirty = vmw_bo_is_dirty(new_bo);
			if (dirty) {
				surf = vmw_user_object_surface(&new_vps->uo);
				if (surf)
					vmw_bo_dirty_transfer_to_res(&surf->res);
				else
					vmw_bo_dirty_clear(new_bo);
			}
			return dirty;
		} else if (new_bo != old_bo) {
			/*
			 * Currently unused because the top exits right away.
			 * In most cases buffer being different will mean
			 * that the contents is different. For the few percent
			 * of cases where that's not true the cost of doing
			 * the memcmp on all other seems to outweight the
			 * benefits. Leave the conditional to be able to
			 * trivially validate it by removing the initial
			 * if (new_bo != old_bo) at the start.
			 */
			void *old_image;
			void *new_image;
			bool changed = false;
			struct ww_acquire_ctx ctx;
			const u32 size = new_vps->base.crtc_w *
					 new_vps->base.crtc_h * sizeof(u32);

			ww_acquire_init(&ctx, &reservation_ww_class);

			ret = ttm_bo_reserve(&old_bo->tbo, false, false, &ctx);
			if (ret != 0) {
				ww_acquire_fini(&ctx);
				return true;
			}

			ret = ttm_bo_reserve(&new_bo->tbo, false, false, &ctx);
			if (ret != 0) {
				ttm_bo_unreserve(&old_bo->tbo);
				ww_acquire_fini(&ctx);
				return true;
			}

			old_image = vmw_bo_map_and_cache(old_bo);
			new_image = vmw_bo_map_and_cache(new_bo);

			if (old_image && new_image && old_image != new_image)
				changed = memcmp(old_image, new_image, size) !=
					  0;

			ttm_bo_unreserve(&new_bo->tbo);
			ttm_bo_unreserve(&old_bo->tbo);

			ww_acquire_fini(&ctx);

			return changed;
		}
		return false;
	}

	return false;
}

static bool
vmw_cursor_plane_changed(struct vmw_plane_state *new_vps,
			 struct vmw_plane_state *old_vps)
{
	if (old_vps->base.crtc_w != new_vps->base.crtc_w ||
	    old_vps->base.crtc_h != new_vps->base.crtc_h)
		return true;

	if (old_vps->base.hotspot_x != new_vps->base.hotspot_x ||
	    old_vps->base.hotspot_y != new_vps->base.hotspot_y)
		return true;

	if (old_vps->cursor.legacy.hotspot_x !=
		    new_vps->cursor.legacy.hotspot_x ||
	    old_vps->cursor.legacy.hotspot_y !=
		    new_vps->cursor.legacy.hotspot_y)
		return true;

	if (old_vps->base.fb != new_vps->base.fb)
		return true;

	return false;
}

/**
 * vmw_cursor_plane_prepare_fb - Readies the cursor by referencing it
 *
 * @plane:  display plane
 * @new_state: info on the new plane state, including the FB
 *
 * Returns 0 on success
 */
int vmw_cursor_plane_prepare_fb(struct drm_plane *plane,
				struct drm_plane_state *new_state)
{
	struct drm_framebuffer *fb = new_state->fb;
	struct vmw_cursor_plane *vcp = vmw_plane_to_vcp(plane);
	struct vmw_plane_state *vps = vmw_plane_state_to_vps(new_state);
	struct vmw_plane_state *old_vps = vmw_plane_state_to_vps(plane->state);
	struct vmw_private *vmw = vmw_priv(plane->dev);
	struct vmw_bo *bo = NULL;
	struct vmw_surface *surface;
	int ret = 0;

	if (!vmw_user_object_is_null(&vps->uo)) {
		vmw_user_object_unmap(&vps->uo);
		vmw_user_object_unref(&vps->uo);
	}

	if (fb) {
		if (vmw_framebuffer_to_vfb(fb)->bo) {
			vps->uo.buffer = vmw_framebuffer_to_vfbd(fb)->buffer;
			vps->uo.surface = NULL;
		} else {
			memcpy(&vps->uo, &vmw_framebuffer_to_vfbs(fb)->uo, sizeof(vps->uo));
		}
		vmw_user_object_ref(&vps->uo);
	}

	vps->cursor.update_type = vmw_cursor_update_type(vmw, vps);
	switch (vps->cursor.update_type) {
	case VMW_CURSOR_UPDATE_LEGACY:
		surface = vmw_user_object_surface(&vps->uo);
		if (!surface || vps->cursor.legacy.id == surface->snooper.id)
			vps->cursor.update_type = VMW_CURSOR_UPDATE_NONE;
		break;
	case VMW_CURSOR_UPDATE_GB_ONLY:
	case VMW_CURSOR_UPDATE_MOB: {
		bo = vmw_user_object_buffer(&vps->uo);
		if (bo) {
			struct ttm_operation_ctx ctx = { false, false };

			ret = ttm_bo_reserve(&bo->tbo, true, false, NULL);
			if (ret != 0)
				return -ENOMEM;

			ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
			if (ret != 0)
				return -ENOMEM;

			/*
			 * vmw_bo_pin_reserved also validates, so to skip
			 * the extra validation use ttm_bo_pin directly
			 */
			if (!bo->tbo.pin_count)
				ttm_bo_pin(&bo->tbo);

			if (vmw_framebuffer_to_vfb(fb)->bo) {
				const u32 size = new_state->crtc_w *
						 new_state->crtc_h *
						 sizeof(u32);

				(void)vmw_bo_map_and_cache_size(bo, size);
			} else {
				vmw_bo_map_and_cache(bo);
			}
			ttm_bo_unreserve(&bo->tbo);
		}
		if (!vmw_user_object_is_null(&vps->uo)) {
			if (!vmw_cursor_plane_changed(vps, old_vps) &&
			    !vmw_cursor_buffer_changed(vps, old_vps)) {
				vps->cursor.update_type =
					VMW_CURSOR_UPDATE_NONE;
			} else {
				vmw_cursor_mob_get(vcp, vps);
				vmw_cursor_mob_map(vps);
			}
		}
	}
		break;
	case VMW_CURSOR_UPDATE_NONE:
		/* do nothing */
		break;
	}

	return 0;
}

/**
 * vmw_cursor_plane_atomic_check - check if the new state is okay
 *
 * @plane: cursor plane
 * @state: info on the new plane state
 *
 * This is a chance to fail if the new cursor state does not fit
 * our requirements.
 *
 * Returns 0 on success
 */
int vmw_cursor_plane_atomic_check(struct drm_plane *plane,
				  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct vmw_private *vmw = vmw_priv(plane->dev);
	int ret = 0;
	struct drm_crtc_state *crtc_state = NULL;
	struct vmw_surface *surface = NULL;
	struct vmw_plane_state *vps = vmw_plane_state_to_vps(new_state);
	enum vmw_cursor_update_type update_type;
	struct drm_framebuffer *fb = new_state->fb;

	if (new_state->crtc)
		crtc_state = drm_atomic_get_new_crtc_state(new_state->state,
							   new_state->crtc);

	ret = drm_atomic_helper_check_plane_state(new_state, crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING, true,
						  true);
	if (ret)
		return ret;

	/* Turning off */
	if (!fb)
		return 0;

	update_type = vmw_cursor_update_type(vmw, vps);
	if (update_type == VMW_CURSOR_UPDATE_LEGACY) {
		if (new_state->crtc_w != VMW_CURSOR_SNOOP_WIDTH ||
		    new_state->crtc_h != VMW_CURSOR_SNOOP_HEIGHT) {
			drm_warn(&vmw->drm,
				 "Invalid cursor dimensions (%d, %d)\n",
				 new_state->crtc_w, new_state->crtc_h);
			return -EINVAL;
		}
		surface = vmw_user_object_surface(&vps->uo);
		if (!surface || !surface->snooper.image) {
			drm_warn(&vmw->drm,
				 "surface not suitable for cursor\n");
			return -EINVAL;
		}
	}

	return 0;
}

void
vmw_cursor_plane_atomic_update(struct drm_plane *plane,
			       struct drm_atomic_state *state)
{
	struct vmw_bo *bo;
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct drm_plane_state *old_state =
		drm_atomic_get_old_plane_state(state, plane);
	struct drm_crtc *crtc = new_state->crtc ?: old_state->crtc;
	struct vmw_private *dev_priv = vmw_priv(plane->dev);
	struct vmw_display_unit *du = vmw_crtc_to_du(crtc);
	struct vmw_plane_state *vps = vmw_plane_state_to_vps(new_state);
	s32 hotspot_x, hotspot_y, cursor_x, cursor_y;

	/*
	 * Hide the cursor if the new bo is null
	 */
	if (vmw_user_object_is_null(&vps->uo)) {
		vmw_cursor_update_position(dev_priv, false, 0, 0);
		return;
	}

	switch (vps->cursor.update_type) {
	case VMW_CURSOR_UPDATE_LEGACY:
		vmw_cursor_plane_update_legacy(dev_priv, vps);
		break;
	case VMW_CURSOR_UPDATE_MOB:
		vmw_cursor_update_mob(dev_priv, vps);
		break;
	case VMW_CURSOR_UPDATE_GB_ONLY:
		bo = vmw_user_object_buffer(&vps->uo);
		if (bo)
			vmw_send_define_cursor_cmd(dev_priv, bo->map.virtual,
						   vps->base.crtc_w,
						   vps->base.crtc_h,
						   vps->base.hotspot_x,
						   vps->base.hotspot_y);
		break;
	case VMW_CURSOR_UPDATE_NONE:
		/* do nothing */
		break;
	}

	/*
	 * For all update types update the cursor position
	 */
	cursor_x = new_state->crtc_x + du->set_gui_x;
	cursor_y = new_state->crtc_y + du->set_gui_y;

	hotspot_x = vps->cursor.legacy.hotspot_x + new_state->hotspot_x;
	hotspot_y = vps->cursor.legacy.hotspot_y + new_state->hotspot_y;

	vmw_cursor_update_position(dev_priv, true, cursor_x + hotspot_x,
				   cursor_y + hotspot_y);
}

int vmw_kms_cursor_bypass_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_vmw_cursor_bypass_arg *arg = data;
	struct vmw_display_unit *du;
	struct vmw_plane_state *vps;
	struct drm_crtc *crtc;
	int ret = 0;

	mutex_lock(&dev->mode_config.mutex);
	if (arg->flags & DRM_VMW_CURSOR_BYPASS_ALL) {
		list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
			du = vmw_crtc_to_du(crtc);
			vps = vmw_plane_state_to_vps(du->cursor.base.state);
			vps->cursor.legacy.hotspot_x = arg->xhot;
			vps->cursor.legacy.hotspot_y = arg->yhot;
		}

		mutex_unlock(&dev->mode_config.mutex);
		return 0;
	}

	crtc = drm_crtc_find(dev, file_priv, arg->crtc_id);
	if (!crtc) {
		ret = -ENOENT;
		goto out;
	}

	du = vmw_crtc_to_du(crtc);
	vps = vmw_plane_state_to_vps(du->cursor.base.state);
	vps->cursor.legacy.hotspot_x = arg->xhot;
	vps->cursor.legacy.hotspot_y = arg->yhot;

out:
	mutex_unlock(&dev->mode_config.mutex);

	return ret;
}

void *vmw_cursor_snooper_create(struct drm_file *file_priv,
				struct vmw_surface_metadata *metadata)
{
	if (!file_priv->atomic && metadata->scanout &&
	    metadata->num_sizes == 1 &&
	    metadata->sizes[0].width == VMW_CURSOR_SNOOP_WIDTH &&
	    metadata->sizes[0].height == VMW_CURSOR_SNOOP_HEIGHT &&
	    metadata->format == VMW_CURSOR_SNOOP_FORMAT) {
		const struct SVGA3dSurfaceDesc *desc =
			vmw_surface_get_desc(VMW_CURSOR_SNOOP_FORMAT);
		const u32 cursor_size_bytes = VMW_CURSOR_SNOOP_WIDTH *
					      VMW_CURSOR_SNOOP_HEIGHT *
					      desc->pitchBytesPerBlock;
		void *image = kzalloc(cursor_size_bytes, GFP_KERNEL);

		if (!image) {
			DRM_ERROR("Failed to allocate cursor_image\n");
			return ERR_PTR(-ENOMEM);
		}
		return image;
	}
	return NULL;
}
