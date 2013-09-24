/* exynos_drm_encoder.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_encoder.h"
#include "exynos_drm_connector.h"

#define to_exynos_encoder(x)	container_of(x, struct exynos_drm_encoder,\
				drm_encoder)

/*
 * exynos specific encoder structure.
 *
 * @drm_encoder: encoder object.
 * @manager: specific encoder has its own manager to control a hardware
 *	appropriately and we can access a hardware drawing on this manager.
 * @dpms: store the encoder dpms value.
 * @updated: indicate whether overlay data updating is needed or not.
 */
struct exynos_drm_encoder {
	struct drm_crtc			*old_crtc;
	struct drm_encoder		drm_encoder;
	struct exynos_drm_manager	*manager;
	int				dpms;
	bool				updated;
};

static void exynos_drm_connector_power(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (exynos_drm_best_encoder(connector) == encoder) {
			DRM_DEBUG_KMS("connector[%d] dpms[%d]\n",
					connector->base.id, mode);

			exynos_drm_display_power(connector, mode);
		}
	}
}

static void exynos_drm_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct exynos_drm_manager *manager = exynos_drm_get_manager(encoder);
	struct exynos_drm_manager_ops *manager_ops = manager->ops;
	struct exynos_drm_encoder *exynos_encoder = to_exynos_encoder(encoder);

	DRM_DEBUG_KMS("encoder dpms: %d\n", mode);

	if (exynos_encoder->dpms == mode) {
		DRM_DEBUG_KMS("desired dpms mode is same as previous one.\n");
		return;
	}

	mutex_lock(&dev->struct_mutex);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		if (manager_ops && manager_ops->apply)
			if (!exynos_encoder->updated)
				manager_ops->apply(manager->dev);

		exynos_drm_connector_power(encoder, mode);
		exynos_encoder->dpms = mode;
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		exynos_drm_connector_power(encoder, mode);
		exynos_encoder->dpms = mode;
		exynos_encoder->updated = false;
		break;
	default:
		DRM_ERROR("unspecified mode %d\n", mode);
		break;
	}

	mutex_unlock(&dev->struct_mutex);
}

static bool
exynos_drm_encoder_mode_fixup(struct drm_encoder *encoder,
			       const struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;
	struct exynos_drm_manager *manager = exynos_drm_get_manager(encoder);
	struct exynos_drm_manager_ops *manager_ops = manager->ops;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder)
			if (manager_ops && manager_ops->mode_fixup)
				manager_ops->mode_fixup(manager->dev, connector,
							mode, adjusted_mode);
	}

	return true;
}

static void disable_plane_to_crtc(struct drm_device *dev,
						struct drm_crtc *old_crtc,
						struct drm_crtc *new_crtc)
{
	struct drm_plane *plane;

	/*
	 * if old_crtc isn't same as encoder->crtc then it means that
	 * user changed crtc id to another one so the plane to old_crtc
	 * should be disabled and plane->crtc should be set to new_crtc
	 * (encoder->crtc)
	 */
	list_for_each_entry(plane, &dev->mode_config.plane_list, head) {
		if (plane->crtc == old_crtc) {
			/*
			 * do not change below call order.
			 *
			 * plane->funcs->disable_plane call checks
			 * if encoder->crtc is same as plane->crtc and if same
			 * then overlay_ops->disable callback will be called
			 * to diasble current hw overlay so plane->crtc should
			 * have new_crtc because new_crtc was set to
			 * encoder->crtc in advance.
			 */
			plane->crtc = new_crtc;
			plane->funcs->disable_plane(plane);
		}
	}
}

static void exynos_drm_encoder_mode_set(struct drm_encoder *encoder,
					 struct drm_display_mode *mode,
					 struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;
	struct exynos_drm_manager *manager;
	struct exynos_drm_manager_ops *manager_ops;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			struct exynos_drm_encoder *exynos_encoder;

			exynos_encoder = to_exynos_encoder(encoder);

			if (exynos_encoder->old_crtc != encoder->crtc &&
					exynos_encoder->old_crtc) {

				/*
				 * disable a plane to old crtc and change
				 * crtc of the plane to new one.
				 */
				disable_plane_to_crtc(dev,
						exynos_encoder->old_crtc,
						encoder->crtc);
			}

			manager = exynos_drm_get_manager(encoder);
			manager_ops = manager->ops;

			if (manager_ops && manager_ops->mode_set)
				manager_ops->mode_set(manager->dev,
							adjusted_mode);

			exynos_encoder->old_crtc = encoder->crtc;
		}
	}
}

static void exynos_drm_encoder_prepare(struct drm_encoder *encoder)
{
	/* drm framework doesn't check NULL. */
}

static void exynos_drm_encoder_commit(struct drm_encoder *encoder)
{
	struct exynos_drm_encoder *exynos_encoder = to_exynos_encoder(encoder);
	struct exynos_drm_manager *manager = exynos_encoder->manager;
	struct exynos_drm_manager_ops *manager_ops = manager->ops;

	if (manager_ops && manager_ops->commit)
		manager_ops->commit(manager->dev);

	/*
	 * this will avoid one issue that overlay data is updated to
	 * real hardware two times.
	 * And this variable will be used to check if the data was
	 * already updated or not by exynos_drm_encoder_dpms function.
	 */
	exynos_encoder->updated = true;

	/*
	 * In case of setcrtc, there is no way to update encoder's dpms
	 * so update it here.
	 */
	exynos_encoder->dpms = DRM_MODE_DPMS_ON;
}

void exynos_drm_encoder_complete_scanout(struct drm_framebuffer *fb)
{
	struct exynos_drm_encoder *exynos_encoder;
	struct exynos_drm_manager_ops *ops;
	struct drm_device *dev = fb->dev;
	struct drm_encoder *encoder;

	/*
	 * make sure that overlay data are updated to real hardware
	 * for all encoders.
	 */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		exynos_encoder = to_exynos_encoder(encoder);
		ops = exynos_encoder->manager->ops;

		/*
		 * wait for vblank interrupt
		 * - this makes sure that overlay data are updated to
		 *	real hardware.
		 */
		if (ops->wait_for_vblank)
			ops->wait_for_vblank(exynos_encoder->manager->dev);
	}
}


static void exynos_drm_encoder_disable(struct drm_encoder *encoder)
{
	struct drm_plane *plane;
	struct drm_device *dev = encoder->dev;

	exynos_drm_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);

	/* all planes connected to this encoder should be also disabled. */
	list_for_each_entry(plane, &dev->mode_config.plane_list, head) {
		if (plane->crtc == encoder->crtc)
			plane->funcs->disable_plane(plane);
	}
}

static struct drm_encoder_helper_funcs exynos_encoder_helper_funcs = {
	.dpms		= exynos_drm_encoder_dpms,
	.mode_fixup	= exynos_drm_encoder_mode_fixup,
	.mode_set	= exynos_drm_encoder_mode_set,
	.prepare	= exynos_drm_encoder_prepare,
	.commit		= exynos_drm_encoder_commit,
	.disable	= exynos_drm_encoder_disable,
};

static void exynos_drm_encoder_destroy(struct drm_encoder *encoder)
{
	struct exynos_drm_encoder *exynos_encoder =
		to_exynos_encoder(encoder);

	exynos_encoder->manager->pipe = -1;

	drm_encoder_cleanup(encoder);
	kfree(exynos_encoder);
}

static struct drm_encoder_funcs exynos_encoder_funcs = {
	.destroy = exynos_drm_encoder_destroy,
};

static unsigned int exynos_drm_encoder_clones(struct drm_encoder *encoder)
{
	struct drm_encoder *clone;
	struct drm_device *dev = encoder->dev;
	struct exynos_drm_encoder *exynos_encoder = to_exynos_encoder(encoder);
	struct exynos_drm_display_ops *display_ops =
				exynos_encoder->manager->display_ops;
	unsigned int clone_mask = 0;
	int cnt = 0;

	list_for_each_entry(clone, &dev->mode_config.encoder_list, head) {
		switch (display_ops->type) {
		case EXYNOS_DISPLAY_TYPE_LCD:
		case EXYNOS_DISPLAY_TYPE_HDMI:
		case EXYNOS_DISPLAY_TYPE_VIDI:
			clone_mask |= (1 << (cnt++));
			break;
		default:
			continue;
		}
	}

	return clone_mask;
}

void exynos_drm_encoder_setup(struct drm_device *dev)
{
	struct drm_encoder *encoder;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head)
		encoder->possible_clones = exynos_drm_encoder_clones(encoder);
}

struct drm_encoder *
exynos_drm_encoder_create(struct drm_device *dev,
			   struct exynos_drm_manager *manager,
			   unsigned int possible_crtcs)
{
	struct drm_encoder *encoder;
	struct exynos_drm_encoder *exynos_encoder;

	if (!manager || !possible_crtcs)
		return NULL;

	if (!manager->dev)
		return NULL;

	exynos_encoder = kzalloc(sizeof(*exynos_encoder), GFP_KERNEL);
	if (!exynos_encoder)
		return NULL;

	exynos_encoder->dpms = DRM_MODE_DPMS_OFF;
	exynos_encoder->manager = manager;
	encoder = &exynos_encoder->drm_encoder;
	encoder->possible_crtcs = possible_crtcs;

	DRM_DEBUG_KMS("possible_crtcs = 0x%x\n", encoder->possible_crtcs);

	drm_encoder_init(dev, encoder, &exynos_encoder_funcs,
			DRM_MODE_ENCODER_TMDS);

	drm_encoder_helper_add(encoder, &exynos_encoder_helper_funcs);

	DRM_DEBUG_KMS("encoder has been created\n");

	return encoder;
}

struct exynos_drm_manager *exynos_drm_get_manager(struct drm_encoder *encoder)
{
	return to_exynos_encoder(encoder)->manager;
}

void exynos_drm_fn_encoder(struct drm_crtc *crtc, void *data,
			    void (*fn)(struct drm_encoder *, void *))
{
	struct drm_device *dev = crtc->dev;
	struct drm_encoder *encoder;
	struct exynos_drm_private *private = dev->dev_private;
	struct exynos_drm_manager *manager;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		/*
		 * if crtc is detached from encoder, check pipe,
		 * otherwise check crtc attached to encoder
		 */
		if (!encoder->crtc) {
			manager = to_exynos_encoder(encoder)->manager;
			if (manager->pipe < 0 ||
					private->crtc[manager->pipe] != crtc)
				continue;
		} else {
			if (encoder->crtc != crtc)
				continue;
		}

		fn(encoder, data);
	}
}

void exynos_drm_enable_vblank(struct drm_encoder *encoder, void *data)
{
	struct exynos_drm_manager *manager =
		to_exynos_encoder(encoder)->manager;
	struct exynos_drm_manager_ops *manager_ops = manager->ops;
	int crtc = *(int *)data;

	if (manager->pipe != crtc)
		return;

	if (manager_ops->enable_vblank)
		manager_ops->enable_vblank(manager->dev);
}

void exynos_drm_disable_vblank(struct drm_encoder *encoder, void *data)
{
	struct exynos_drm_manager *manager =
		to_exynos_encoder(encoder)->manager;
	struct exynos_drm_manager_ops *manager_ops = manager->ops;
	int crtc = *(int *)data;

	if (manager->pipe != crtc)
		return;

	if (manager_ops->disable_vblank)
		manager_ops->disable_vblank(manager->dev);
}

void exynos_drm_encoder_crtc_dpms(struct drm_encoder *encoder, void *data)
{
	struct exynos_drm_encoder *exynos_encoder = to_exynos_encoder(encoder);
	struct exynos_drm_manager *manager = exynos_encoder->manager;
	struct exynos_drm_manager_ops *manager_ops = manager->ops;
	int mode = *(int *)data;

	if (manager_ops && manager_ops->dpms)
		manager_ops->dpms(manager->dev, mode);

	/*
	 * if this condition is ok then it means that the crtc is already
	 * detached from encoder and last function for detaching is properly
	 * done, so clear pipe from manager to prevent repeated call.
	 */
	if (mode > DRM_MODE_DPMS_ON) {
		if (!encoder->crtc)
			manager->pipe = -1;
	}
}

void exynos_drm_encoder_crtc_pipe(struct drm_encoder *encoder, void *data)
{
	struct exynos_drm_manager *manager =
		to_exynos_encoder(encoder)->manager;
	int pipe = *(int *)data;

	/*
	 * when crtc is detached from encoder, this pipe is used
	 * to select manager operation
	 */
	manager->pipe = pipe;
}

void exynos_drm_encoder_plane_mode_set(struct drm_encoder *encoder, void *data)
{
	struct exynos_drm_manager *manager =
		to_exynos_encoder(encoder)->manager;
	struct exynos_drm_overlay_ops *overlay_ops = manager->overlay_ops;
	struct exynos_drm_overlay *overlay = data;

	if (overlay_ops && overlay_ops->mode_set)
		overlay_ops->mode_set(manager->dev, overlay);
}

void exynos_drm_encoder_plane_commit(struct drm_encoder *encoder, void *data)
{
	struct exynos_drm_manager *manager =
		to_exynos_encoder(encoder)->manager;
	struct exynos_drm_overlay_ops *overlay_ops = manager->overlay_ops;
	int zpos = DEFAULT_ZPOS;

	if (data)
		zpos = *(int *)data;

	if (overlay_ops && overlay_ops->commit)
		overlay_ops->commit(manager->dev, zpos);
}

void exynos_drm_encoder_plane_enable(struct drm_encoder *encoder, void *data)
{
	struct exynos_drm_manager *manager =
		to_exynos_encoder(encoder)->manager;
	struct exynos_drm_overlay_ops *overlay_ops = manager->overlay_ops;
	int zpos = DEFAULT_ZPOS;

	if (data)
		zpos = *(int *)data;

	if (overlay_ops && overlay_ops->enable)
		overlay_ops->enable(manager->dev, zpos);
}

void exynos_drm_encoder_plane_disable(struct drm_encoder *encoder, void *data)
{
	struct exynos_drm_manager *manager =
		to_exynos_encoder(encoder)->manager;
	struct exynos_drm_overlay_ops *overlay_ops = manager->overlay_ops;
	int zpos = DEFAULT_ZPOS;

	if (data)
		zpos = *(int *)data;

	if (overlay_ops && overlay_ops->disable)
		overlay_ops->disable(manager->dev, zpos);
}
