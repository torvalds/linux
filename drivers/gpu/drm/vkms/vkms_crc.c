// SPDX-License-Identifier: GPL-2.0
#include "vkms_drv.h"
#include <linux/crc32.h>
#include <drm/drm_gem_framebuffer_helper.h>

static uint32_t _vkms_get_crc(struct vkms_crc_data *crc_data)
{
	struct drm_framebuffer *fb = &crc_data->fb;
	struct drm_gem_object *gem_obj = drm_gem_fb_get_obj(fb, 0);
	struct vkms_gem_object *vkms_obj = drm_gem_to_vkms_gem(gem_obj);
	u32 crc = 0;
	int i = 0;
	unsigned int x = crc_data->src.x1 >> 16;
	unsigned int y = crc_data->src.y1 >> 16;
	unsigned int height = drm_rect_height(&crc_data->src) >> 16;
	unsigned int width = drm_rect_width(&crc_data->src) >> 16;
	unsigned int cpp = fb->format->cpp[0];
	unsigned int src_offset;
	unsigned int size_byte = width * cpp;
	void *vaddr;

	mutex_lock(&vkms_obj->pages_lock);
	vaddr = vkms_obj->vaddr;
	if (WARN_ON(!vaddr))
		goto out;

	for (i = y; i < y + height; i++) {
		src_offset = fb->offsets[0] + (i * fb->pitches[0]) + (x * cpp);
		crc = crc32_le(crc, vaddr + src_offset, size_byte);
	}

out:
	mutex_unlock(&vkms_obj->pages_lock);
	return crc;
}

void vkms_crc_work_handle(struct work_struct *work)
{
	struct vkms_crtc_state *crtc_state = container_of(work,
						struct vkms_crtc_state,
						crc_work);
	struct drm_crtc *crtc = crtc_state->base.crtc;
	struct vkms_output *out = drm_crtc_to_vkms_output(crtc);
	struct vkms_device *vdev = container_of(out, struct vkms_device,
						output);
	struct vkms_crc_data *primary_crc = NULL;
	struct drm_plane *plane;

	u32 crc32 = 0;

	drm_for_each_plane(plane, &vdev->drm) {
		struct vkms_plane_state *vplane_state;
		struct vkms_crc_data *crc_data;

		vplane_state = to_vkms_plane_state(plane->state);
		crc_data = vplane_state->crc_data;

		if (drm_framebuffer_read_refcount(&crc_data->fb) == 0)
			continue;

		if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
			primary_crc = crc_data;
			break;
		}
	}

	if (primary_crc)
		crc32 = _vkms_get_crc(primary_crc);

	drm_crtc_add_crc_entry(crtc, true, crtc_state->n_frame, &crc32);
}

int vkms_set_crc_source(struct drm_crtc *crtc, const char *src_name,
			size_t *values_cnt)
{
	struct vkms_output *out = drm_crtc_to_vkms_output(crtc);
	bool enabled = false;
	unsigned long flags;
	int ret = 0;

	if (src_name && strcmp(src_name, "auto") == 0)
		enabled = true;
	else if (src_name)
		ret = -EINVAL;

	*values_cnt = 1;

	/* make sure nothing is scheduled on crtc workq */
	flush_workqueue(out->crc_workq);

	spin_lock_irqsave(&out->lock, flags);
	out->crc_enabled = enabled;
	spin_unlock_irqrestore(&out->lock, flags);

	return ret;
}
