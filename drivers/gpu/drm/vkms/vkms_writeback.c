// SPDX-License-Identifier: GPL-2.0+

#include <linux/dma-buf-map.h>

#include <drm/drm_atomic.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_writeback.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>

#include "vkms_drv.h"

static const u32 vkms_wb_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const struct drm_connector_funcs vkms_wb_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int vkms_wb_encoder_atomic_check(struct drm_encoder *encoder,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	struct drm_framebuffer *fb;
	const struct drm_display_mode *mode = &crtc_state->mode;

	if (!conn_state->writeback_job || !conn_state->writeback_job->fb)
		return 0;

	fb = conn_state->writeback_job->fb;
	if (fb->width != mode->hdisplay || fb->height != mode->vdisplay) {
		DRM_DEBUG_KMS("Invalid framebuffer size %ux%u\n",
			      fb->width, fb->height);
		return -EINVAL;
	}

	if (fb->format->format != vkms_wb_formats[0]) {
		DRM_DEBUG_KMS("Invalid pixel format %p4cc\n",
			      &fb->format->format);
		return -EINVAL;
	}

	return 0;
}

static const struct drm_encoder_helper_funcs vkms_wb_encoder_helper_funcs = {
	.atomic_check = vkms_wb_encoder_atomic_check,
};

static int vkms_wb_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	return drm_add_modes_noedid(connector, dev->mode_config.max_width,
				    dev->mode_config.max_height);
}

static int vkms_wb_prepare_job(struct drm_writeback_connector *wb_connector,
			       struct drm_writeback_job *job)
{
	struct drm_gem_object *gem_obj;
	struct dma_buf_map map;
	int ret;

	if (!job->fb)
		return 0;

	gem_obj = drm_gem_fb_get_obj(job->fb, 0);
	ret = drm_gem_shmem_vmap(gem_obj, &map);
	if (ret) {
		DRM_ERROR("vmap failed: %d\n", ret);
		return ret;
	}

	job->priv = map.vaddr;

	return 0;
}

static void vkms_wb_cleanup_job(struct drm_writeback_connector *connector,
				struct drm_writeback_job *job)
{
	struct drm_gem_object *gem_obj;
	struct vkms_device *vkmsdev;
	struct dma_buf_map map;

	if (!job->fb)
		return;

	gem_obj = drm_gem_fb_get_obj(job->fb, 0);
	dma_buf_map_set_vaddr(&map, job->priv);
	drm_gem_shmem_vunmap(gem_obj, &map);

	vkmsdev = drm_device_to_vkms_device(gem_obj->dev);
	vkms_set_composer(&vkmsdev->output, false);
}

static void vkms_wb_atomic_commit(struct drm_connector *conn,
				  struct drm_atomic_state *state)
{
	struct drm_connector_state *connector_state = drm_atomic_get_new_connector_state(state,
											 conn);
	struct vkms_device *vkmsdev = drm_device_to_vkms_device(conn->dev);
	struct vkms_output *output = &vkmsdev->output;
	struct drm_writeback_connector *wb_conn = &output->wb_connector;
	struct drm_connector_state *conn_state = wb_conn->base.state;
	struct vkms_crtc_state *crtc_state = output->composer_state;

	if (!conn_state)
		return;

	vkms_set_composer(&vkmsdev->output, true);

	spin_lock_irq(&output->composer_lock);
	crtc_state->active_writeback = conn_state->writeback_job->priv;
	crtc_state->wb_pending = true;
	spin_unlock_irq(&output->composer_lock);
	drm_writeback_queue_job(wb_conn, connector_state);
}

static const struct drm_connector_helper_funcs vkms_wb_conn_helper_funcs = {
	.get_modes = vkms_wb_connector_get_modes,
	.prepare_writeback_job = vkms_wb_prepare_job,
	.cleanup_writeback_job = vkms_wb_cleanup_job,
	.atomic_commit = vkms_wb_atomic_commit,
};

int vkms_enable_writeback_connector(struct vkms_device *vkmsdev)
{
	struct drm_writeback_connector *wb = &vkmsdev->output.wb_connector;

	vkmsdev->output.wb_connector.encoder.possible_crtcs = 1;
	drm_connector_helper_add(&wb->base, &vkms_wb_conn_helper_funcs);

	return drm_writeback_connector_init(&vkmsdev->drm, wb,
					    &vkms_wb_connector_funcs,
					    &vkms_wb_encoder_helper_funcs,
					    vkms_wb_formats,
					    ARRAY_SIZE(vkms_wb_formats));
}
