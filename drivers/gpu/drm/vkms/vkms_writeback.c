// SPDX-License-Identifier: GPL-2.0+

#include <linux/iosys-map.h>

#include <drm/drm_atomic.h>
#include <drm/drm_edid.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_writeback.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>

#include "vkms_drv.h"
#include "vkms_formats.h"

static const u32 vkms_wb_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XRGB16161616,
	DRM_FORMAT_ARGB16161616,
	DRM_FORMAT_RGB565
};

static const struct drm_connector_funcs vkms_wb_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int vkms_wb_atomic_check(struct drm_connector *connector,
				struct drm_atomic_state *state)
{
	struct drm_connector_state *conn_state =
		drm_atomic_get_new_connector_state(state, connector);
	struct drm_crtc_state *crtc_state;
	struct drm_framebuffer *fb;
	const struct drm_display_mode *mode;
	int ret;

	if (!conn_state->writeback_job || !conn_state->writeback_job->fb)
		return 0;

	if (!conn_state->crtc)
		return 0;

	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	mode = &crtc_state->mode;

	fb = conn_state->writeback_job->fb;
	if (fb->width != mode->hdisplay || fb->height != mode->vdisplay) {
		DRM_DEBUG_KMS("Invalid framebuffer size %ux%u\n",
			      fb->width, fb->height);
		return -EINVAL;
	}

	ret = drm_atomic_helper_check_wb_connector_state(connector, state);
	if (ret < 0)
		return ret;

	return 0;
}

static int vkms_wb_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	return drm_add_modes_noedid(connector, dev->mode_config.max_width,
				    dev->mode_config.max_height);
}

static int vkms_wb_prepare_job(struct drm_writeback_connector *wb_connector,
			       struct drm_writeback_job *job)
{
	struct vkms_writeback_job *vkmsjob;
	int ret;

	if (!job->fb)
		return 0;

	vkmsjob = kzalloc(sizeof(*vkmsjob), GFP_KERNEL);
	if (!vkmsjob)
		return -ENOMEM;

	ret = drm_gem_fb_vmap(job->fb, vkmsjob->wb_frame_info.map, vkmsjob->data);
	if (ret) {
		DRM_ERROR("vmap failed: %d\n", ret);
		goto err_kfree;
	}

	vkmsjob->wb_frame_info.fb = job->fb;
	drm_framebuffer_get(vkmsjob->wb_frame_info.fb);

	job->priv = vkmsjob;

	return 0;

err_kfree:
	kfree(vkmsjob);
	return ret;
}

static void vkms_wb_cleanup_job(struct drm_writeback_connector *connector,
				struct drm_writeback_job *job)
{
	struct vkms_writeback_job *vkmsjob = job->priv;
	struct vkms_device *vkmsdev;

	if (!job->fb)
		return;

	drm_gem_fb_vunmap(job->fb, vkmsjob->wb_frame_info.map);

	drm_framebuffer_put(vkmsjob->wb_frame_info.fb);

	vkmsdev = drm_device_to_vkms_device(job->fb->dev);
	vkms_set_composer(&vkmsdev->output, false);
	kfree(vkmsjob);
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
	struct drm_framebuffer *fb = connector_state->writeback_job->fb;
	u16 crtc_height = crtc_state->base.mode.vdisplay;
	u16 crtc_width = crtc_state->base.mode.hdisplay;
	struct vkms_writeback_job *active_wb;
	struct vkms_frame_info *wb_frame_info;
	u32 wb_format = fb->format->format;

	if (!conn_state)
		return;

	vkms_set_composer(&vkmsdev->output, true);

	active_wb = conn_state->writeback_job->priv;
	wb_frame_info = &active_wb->wb_frame_info;

	spin_lock_irq(&output->composer_lock);
	crtc_state->active_writeback = active_wb;
	crtc_state->wb_pending = true;
	spin_unlock_irq(&output->composer_lock);
	drm_writeback_queue_job(wb_conn, connector_state);
	active_wb->pixel_write = get_pixel_write_function(wb_format);
	drm_rect_init(&wb_frame_info->src, 0, 0, crtc_width, crtc_height);
	drm_rect_init(&wb_frame_info->dst, 0, 0, crtc_width, crtc_height);
}

static const struct drm_connector_helper_funcs vkms_wb_conn_helper_funcs = {
	.get_modes = vkms_wb_connector_get_modes,
	.prepare_writeback_job = vkms_wb_prepare_job,
	.cleanup_writeback_job = vkms_wb_cleanup_job,
	.atomic_commit = vkms_wb_atomic_commit,
	.atomic_check = vkms_wb_atomic_check,
};

int vkms_enable_writeback_connector(struct vkms_device *vkmsdev)
{
	struct drm_writeback_connector *wb = &vkmsdev->output.wb_connector;

	drm_connector_helper_add(&wb->base, &vkms_wb_conn_helper_funcs);

	return drm_writeback_connector_init(&vkmsdev->drm, wb,
					    &vkms_wb_connector_funcs,
					    NULL,
					    vkms_wb_formats,
					    ARRAY_SIZE(vkms_wb_formats),
					    1);
}
