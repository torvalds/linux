/*
 * Copyright (C) 2013-2017 Oracle Corporation
 * This file is based on ast_main.c
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * Authors: Dave Airlie <airlied@redhat.com>,
 *          Michael Thayer <michael.thayer@oracle.com,
 *          Hans de Goede <hdegoede@redhat.com>
 */
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>

#include "vbox_drv.h"
#include "vbox_err.h"
#include "vboxvideo_guest.h"
#include "vboxvideo_vbe.h"

static void vbox_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct vbox_framebuffer *vbox_fb = to_vbox_framebuffer(fb);

	if (vbox_fb->obj)
		drm_gem_object_put_unlocked(vbox_fb->obj);

	drm_framebuffer_cleanup(fb);
	kfree(fb);
}

void vbox_enable_accel(struct vbox_private *vbox)
{
	unsigned int i;
	struct vbva_buffer *vbva;

	if (!vbox->vbva_info || !vbox->vbva_buffers) {
		/* Should never happen... */
		DRM_ERROR("vboxvideo: failed to set up VBVA.\n");
		return;
	}

	for (i = 0; i < vbox->num_crtcs; ++i) {
		if (vbox->vbva_info[i].vbva)
			continue;

		vbva = (void __force *)vbox->vbva_buffers +
			i * VBVA_MIN_BUFFER_SIZE;
		if (!vbva_enable(&vbox->vbva_info[i],
				 vbox->guest_pool, vbva, i)) {
			/* very old host or driver error. */
			DRM_ERROR("vboxvideo: vbva_enable failed\n");
			return;
		}
	}
}

void vbox_disable_accel(struct vbox_private *vbox)
{
	unsigned int i;

	for (i = 0; i < vbox->num_crtcs; ++i)
		vbva_disable(&vbox->vbva_info[i], vbox->guest_pool, i);
}

void vbox_report_caps(struct vbox_private *vbox)
{
	u32 caps = VBVACAPS_DISABLE_CURSOR_INTEGRATION |
		   VBVACAPS_IRQ | VBVACAPS_USE_VBVA_ONLY;

	if (vbox->initial_mode_queried)
		caps |= VBVACAPS_VIDEO_MODE_HINTS;

	hgsmi_send_caps_info(vbox->guest_pool, caps);
}

/**
 * Send information about dirty rectangles to VBVA.  If necessary we enable
 * VBVA first, as this is normally disabled after a change of master in case
 * the new master does not send dirty rectangle information (is this even
 * allowed?)
 */
void vbox_framebuffer_dirty_rectangles(struct drm_framebuffer *fb,
				       struct drm_clip_rect *rects,
				       unsigned int num_rects)
{
	struct vbox_private *vbox = fb->dev->dev_private;
	struct drm_display_mode *mode;
	struct drm_crtc *crtc;
	int crtc_x, crtc_y;
	unsigned int i;

	mutex_lock(&vbox->hw_mutex);
	list_for_each_entry(crtc, &fb->dev->mode_config.crtc_list, head) {
		if (crtc->primary->state->fb != fb)
			continue;

		mode = &crtc->state->mode;
		crtc_x = crtc->primary->state->src_x >> 16;
		crtc_y = crtc->primary->state->src_y >> 16;

		vbox_enable_accel(vbox);

		for (i = 0; i < num_rects; ++i) {
			struct vbva_cmd_hdr cmd_hdr;
			unsigned int crtc_id = to_vbox_crtc(crtc)->crtc_id;

			if ((rects[i].x1 > crtc_x + mode->hdisplay) ||
			    (rects[i].y1 > crtc_y + mode->vdisplay) ||
			    (rects[i].x2 < crtc_x) ||
			    (rects[i].y2 < crtc_y))
				continue;

			cmd_hdr.x = (s16)rects[i].x1;
			cmd_hdr.y = (s16)rects[i].y1;
			cmd_hdr.w = (u16)rects[i].x2 - rects[i].x1;
			cmd_hdr.h = (u16)rects[i].y2 - rects[i].y1;

			if (!vbva_buffer_begin_update(&vbox->vbva_info[crtc_id],
						      vbox->guest_pool))
				continue;

			vbva_write(&vbox->vbva_info[crtc_id], vbox->guest_pool,
				   &cmd_hdr, sizeof(cmd_hdr));
			vbva_buffer_end_update(&vbox->vbva_info[crtc_id]);
		}
	}
	mutex_unlock(&vbox->hw_mutex);
}

static int vbox_user_framebuffer_dirty(struct drm_framebuffer *fb,
				       struct drm_file *file_priv,
				       unsigned int flags, unsigned int color,
				       struct drm_clip_rect *rects,
				       unsigned int num_rects)
{
	vbox_framebuffer_dirty_rectangles(fb, rects, num_rects);

	return 0;
}

static const struct drm_framebuffer_funcs vbox_fb_funcs = {
	.destroy = vbox_user_framebuffer_destroy,
	.dirty = vbox_user_framebuffer_dirty,
};

int vbox_framebuffer_init(struct vbox_private *vbox,
			  struct vbox_framebuffer *vbox_fb,
			  const struct DRM_MODE_FB_CMD *mode_cmd,
			  struct drm_gem_object *obj)
{
	int ret;

	drm_helper_mode_fill_fb_struct(&vbox->ddev, &vbox_fb->base, mode_cmd);
	vbox_fb->obj = obj;
	ret = drm_framebuffer_init(&vbox->ddev, &vbox_fb->base, &vbox_fb_funcs);
	if (ret) {
		DRM_ERROR("framebuffer init failed %d\n", ret);
		return ret;
	}

	return 0;
}

static int vbox_accel_init(struct vbox_private *vbox)
{
	unsigned int i;

	vbox->vbva_info = devm_kcalloc(vbox->ddev.dev, vbox->num_crtcs,
				       sizeof(*vbox->vbva_info), GFP_KERNEL);
	if (!vbox->vbva_info)
		return -ENOMEM;

	/* Take a command buffer for each screen from the end of usable VRAM. */
	vbox->available_vram_size -= vbox->num_crtcs * VBVA_MIN_BUFFER_SIZE;

	vbox->vbva_buffers = pci_iomap_range(vbox->ddev.pdev, 0,
					     vbox->available_vram_size,
					     vbox->num_crtcs *
					     VBVA_MIN_BUFFER_SIZE);
	if (!vbox->vbva_buffers)
		return -ENOMEM;

	for (i = 0; i < vbox->num_crtcs; ++i)
		vbva_setup_buffer_context(&vbox->vbva_info[i],
					  vbox->available_vram_size +
					  i * VBVA_MIN_BUFFER_SIZE,
					  VBVA_MIN_BUFFER_SIZE);

	return 0;
}

static void vbox_accel_fini(struct vbox_private *vbox)
{
	vbox_disable_accel(vbox);
	pci_iounmap(vbox->ddev.pdev, vbox->vbva_buffers);
}

/** Do we support the 4.3 plus mode hint reporting interface? */
static bool have_hgsmi_mode_hints(struct vbox_private *vbox)
{
	u32 have_hints, have_cursor;
	int ret;

	ret = hgsmi_query_conf(vbox->guest_pool,
			       VBOX_VBVA_CONF32_MODE_HINT_REPORTING,
			       &have_hints);
	if (ret)
		return false;

	ret = hgsmi_query_conf(vbox->guest_pool,
			       VBOX_VBVA_CONF32_GUEST_CURSOR_REPORTING,
			       &have_cursor);
	if (ret)
		return false;

	return have_hints == VINF_SUCCESS && have_cursor == VINF_SUCCESS;
}

bool vbox_check_supported(u16 id)
{
	u16 dispi_id;

	vbox_write_ioport(VBE_DISPI_INDEX_ID, id);
	dispi_id = inw(VBE_DISPI_IOPORT_DATA);

	return dispi_id == id;
}

/**
 * Set up our heaps and data exchange buffers in VRAM before handing the rest
 * to the memory manager.
 */
int vbox_hw_init(struct vbox_private *vbox)
{
	int ret = -ENOMEM;

	vbox->full_vram_size = inl(VBE_DISPI_IOPORT_DATA);
	vbox->any_pitch = vbox_check_supported(VBE_DISPI_ID_ANYX);

	DRM_INFO("VRAM %08x\n", vbox->full_vram_size);

	/* Map guest-heap at end of vram */
	vbox->guest_heap =
	    pci_iomap_range(vbox->ddev.pdev, 0, GUEST_HEAP_OFFSET(vbox),
			    GUEST_HEAP_SIZE);
	if (!vbox->guest_heap)
		return -ENOMEM;

	/* Create guest-heap mem-pool use 2^4 = 16 byte chunks */
	vbox->guest_pool = gen_pool_create(4, -1);
	if (!vbox->guest_pool)
		goto err_unmap_guest_heap;

	ret = gen_pool_add_virt(vbox->guest_pool,
				(unsigned long)vbox->guest_heap,
				GUEST_HEAP_OFFSET(vbox),
				GUEST_HEAP_USABLE_SIZE, -1);
	if (ret)
		goto err_destroy_guest_pool;

	ret = hgsmi_test_query_conf(vbox->guest_pool);
	if (ret) {
		DRM_ERROR("vboxvideo: hgsmi_test_query_conf failed\n");
		goto err_destroy_guest_pool;
	}

	/* Reduce available VRAM size to reflect the guest heap. */
	vbox->available_vram_size = GUEST_HEAP_OFFSET(vbox);
	/* Linux drm represents monitors as a 32-bit array. */
	hgsmi_query_conf(vbox->guest_pool, VBOX_VBVA_CONF32_MONITOR_COUNT,
			 &vbox->num_crtcs);
	vbox->num_crtcs = clamp_t(u32, vbox->num_crtcs, 1, VBOX_MAX_SCREENS);

	if (!have_hgsmi_mode_hints(vbox)) {
		ret = -ENOTSUPP;
		goto err_destroy_guest_pool;
	}

	vbox->last_mode_hints = devm_kcalloc(vbox->ddev.dev, vbox->num_crtcs,
					     sizeof(struct vbva_modehint),
					     GFP_KERNEL);
	if (!vbox->last_mode_hints) {
		ret = -ENOMEM;
		goto err_destroy_guest_pool;
	}

	ret = vbox_accel_init(vbox);
	if (ret)
		goto err_destroy_guest_pool;

	return 0;

err_destroy_guest_pool:
	gen_pool_destroy(vbox->guest_pool);
err_unmap_guest_heap:
	pci_iounmap(vbox->ddev.pdev, vbox->guest_heap);
	return ret;
}

void vbox_hw_fini(struct vbox_private *vbox)
{
	vbox_accel_fini(vbox);
	gen_pool_destroy(vbox->guest_pool);
	pci_iounmap(vbox->ddev.pdev, vbox->guest_heap);
}

int vbox_gem_create(struct vbox_private *vbox,
		    u32 size, bool iskernel, struct drm_gem_object **obj)
{
	struct vbox_bo *vboxbo;
	int ret;

	*obj = NULL;

	size = roundup(size, PAGE_SIZE);
	if (size == 0)
		return -EINVAL;

	ret = vbox_bo_create(vbox, size, 0, 0, &vboxbo);
	if (ret) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("failed to allocate GEM object\n");
		return ret;
	}

	*obj = &vboxbo->gem;

	return 0;
}

int vbox_dumb_create(struct drm_file *file,
		     struct drm_device *dev, struct drm_mode_create_dumb *args)
{
	struct vbox_private *vbox =
		container_of(dev, struct vbox_private, ddev);
	struct drm_gem_object *gobj;
	u32 handle;
	int ret;

	args->pitch = args->width * ((args->bpp + 7) / 8);
	args->size = args->pitch * args->height;

	ret = vbox_gem_create(vbox, args->size, false, &gobj);
	if (ret)
		return ret;

	ret = drm_gem_handle_create(file, gobj, &handle);
	drm_gem_object_put_unlocked(gobj);
	if (ret)
		return ret;

	args->handle = handle;

	return 0;
}

void vbox_gem_free_object(struct drm_gem_object *obj)
{
	struct vbox_bo *vbox_bo = gem_to_vbox_bo(obj);

	ttm_bo_put(&vbox_bo->bo);
}

static inline u64 vbox_bo_mmap_offset(struct vbox_bo *bo)
{
	return drm_vma_node_offset_addr(&bo->bo.vma_node);
}

int
vbox_dumb_mmap_offset(struct drm_file *file,
		      struct drm_device *dev,
		      u32 handle, u64 *offset)
{
	struct drm_gem_object *obj;
	int ret;
	struct vbox_bo *bo;

	mutex_lock(&dev->struct_mutex);
	obj = drm_gem_object_lookup(file, handle);
	if (!obj) {
		ret = -ENOENT;
		goto out_unlock;
	}

	bo = gem_to_vbox_bo(obj);
	*offset = vbox_bo_mmap_offset(bo);

	drm_gem_object_put(obj);
	ret = 0;

out_unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}
