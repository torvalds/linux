/*
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 *
 * based on exynos_drm_encoder.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_encoder.h"
#include "rockchip_drm_connector.h"

#define to_rockchip_encoder(x)	container_of(x, struct rockchip_drm_encoder,\
				drm_encoder)

/*
 * rockchip specific encoder structure.
 *
 * @drm_encoder: encoder object.
 * @manager: specific encoder has its own manager to control a hardware
 *	appropriately and we can access a hardware drawing on this manager.
 * @dpms: store the encoder dpms value.
 * @updated: indicate whether overlay data updating is needed or not.
 */
struct rockchip_drm_encoder {
	struct drm_crtc			*old_crtc;
	struct drm_encoder		drm_encoder;
	struct rockchip_drm_manager	*manager;
	int				dpms;
	bool				updated;
};

static void rockchip_drm_connector_power(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (rockchip_drm_best_encoder(connector) == encoder) {
			DRM_DEBUG_KMS("connector[%d] dpms[%d]\n",
					connector->base.id, mode);

			rockchip_drm_display_power(connector, mode);
		}
	}
}

static void rockchip_drm_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct rockchip_drm_manager *manager = rockchip_drm_get_manager(encoder);
	struct rockchip_drm_manager_ops *manager_ops = manager->ops;
	struct rockchip_drm_encoder *rockchip_encoder = to_rockchip_encoder(encoder);

	DRM_DEBUG_KMS("%s, encoder dpms: %d\n", __FILE__, mode);

	if (rockchip_encoder->dpms == mode) {
		DRM_DEBUG_KMS("desired dpms mode is same as previous one.\n");
		return;
	}

	mutex_lock(&dev->struct_mutex);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		if (manager_ops && manager_ops->apply)
			if (!rockchip_encoder->updated)
				manager_ops->apply(manager->dev);

		rockchip_drm_connector_power(encoder, mode);
		rockchip_encoder->dpms = mode;
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		rockchip_drm_connector_power(encoder, mode);
		rockchip_encoder->dpms = mode;
		rockchip_encoder->updated = false;
		break;
	default:
		DRM_ERROR("unspecified mode %d\n", mode);
		break;
	}

	mutex_unlock(&dev->struct_mutex);
}

static bool
rockchip_drm_encoder_mode_fixup(struct drm_encoder *encoder,
			       const struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;
	struct rockchip_drm_manager *manager = rockchip_drm_get_manager(encoder);
	struct rockchip_drm_manager_ops *manager_ops = manager->ops;

	DRM_DEBUG_KMS("%s\n", __FILE__);

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

static void rockchip_drm_encoder_mode_set(struct drm_encoder *encoder,
					 struct drm_display_mode *mode,
					 struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;
	struct rockchip_drm_manager *manager;
	struct rockchip_drm_manager_ops *manager_ops;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			struct rockchip_drm_encoder *rockchip_encoder;

			rockchip_encoder = to_rockchip_encoder(encoder);

			if (rockchip_encoder->old_crtc != encoder->crtc &&
					rockchip_encoder->old_crtc) {

				/*
				 * disable a plane to old crtc and change
				 * crtc of the plane to new one.
				 */
				disable_plane_to_crtc(dev,
						rockchip_encoder->old_crtc,
						encoder->crtc);
			}

			manager = rockchip_drm_get_manager(encoder);
			manager_ops = manager->ops;

			if (manager_ops && manager_ops->mode_set)
				manager_ops->mode_set(manager->dev,
							adjusted_mode);

			rockchip_encoder->old_crtc = encoder->crtc;
		}
	}
}

static void rockchip_drm_encoder_prepare(struct drm_encoder *encoder)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* drm framework doesn't check NULL. */
}

static void rockchip_drm_encoder_commit(struct drm_encoder *encoder)
{
	struct rockchip_drm_encoder *rockchip_encoder = to_rockchip_encoder(encoder);
	struct rockchip_drm_manager *manager = rockchip_encoder->manager;
	struct rockchip_drm_manager_ops *manager_ops = manager->ops;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (manager_ops && manager_ops->commit)
		manager_ops->commit(manager->dev);

	/*
	 * this will avoid one issue that overlay data is updated to
	 * real hardware two times.
	 * And this variable will be used to check if the data was
	 * already updated or not by rockchip_drm_encoder_dpms function.
	 */
	rockchip_encoder->updated = true;

	/*
	 * In case of setcrtc, there is no way to update encoder's dpms
	 * so update it here.
	 */
	rockchip_encoder->dpms = DRM_MODE_DPMS_ON;
}

void rockchip_drm_encoder_complete_scanout(struct drm_framebuffer *fb)
{
	struct rockchip_drm_encoder *rockchip_encoder;
	struct rockchip_drm_manager_ops *ops;
	struct drm_device *dev = fb->dev;
	struct drm_encoder *encoder;

	/*
	 * make sure that overlay data are updated to real hardware
	 * for all encoders.
	 */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		rockchip_encoder = to_rockchip_encoder(encoder);
		ops = rockchip_encoder->manager->ops;

		/*
		 * wait for vblank interrupt
		 * - this makes sure that overlay data are updated to
		 *	real hardware.
		 */
		if (ops->wait_for_vblank)
			ops->wait_for_vblank(rockchip_encoder->manager->dev);
	}
}


static void rockchip_drm_encoder_disable(struct drm_encoder *encoder)
{
	struct drm_plane *plane;
	struct drm_device *dev = encoder->dev;

	rockchip_drm_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);

	/* all planes connected to this encoder should be also disabled. */
	list_for_each_entry(plane, &dev->mode_config.plane_list, head) {
		if (plane->crtc == encoder->crtc)
			plane->funcs->disable_plane(plane);
	}
}

static struct drm_encoder_helper_funcs rockchip_encoder_helper_funcs = {
	.dpms		= rockchip_drm_encoder_dpms,
	.mode_fixup	= rockchip_drm_encoder_mode_fixup,
	.mode_set	= rockchip_drm_encoder_mode_set,
	.prepare	= rockchip_drm_encoder_prepare,
	.commit		= rockchip_drm_encoder_commit,
	.disable	= rockchip_drm_encoder_disable,
};

static void rockchip_drm_encoder_destroy(struct drm_encoder *encoder)
{
	struct rockchip_drm_encoder *rockchip_encoder =
		to_rockchip_encoder(encoder);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	rockchip_encoder->manager->pipe = -1;

	drm_encoder_cleanup(encoder);
	kfree(rockchip_encoder);
}

static struct drm_encoder_funcs rockchip_encoder_funcs = {
	.destroy = rockchip_drm_encoder_destroy,
};

static unsigned int rockchip_drm_encoder_clones(struct drm_encoder *encoder)
{
	struct drm_encoder *clone;
	struct drm_device *dev = encoder->dev;
	struct rockchip_drm_encoder *rockchip_encoder = to_rockchip_encoder(encoder);
	struct rockchip_drm_display_ops *display_ops =
				rockchip_encoder->manager->display_ops;
	unsigned int clone_mask = 0;
	int cnt = 0;

	list_for_each_entry(clone, &dev->mode_config.encoder_list, head) {
		switch (display_ops->type) {
		case ROCKCHIP_DISPLAY_TYPE_LCD:
		case ROCKCHIP_DISPLAY_TYPE_HDMI:
		case ROCKCHIP_DISPLAY_TYPE_VIDI:
			clone_mask |= (1 << (cnt++));
			break;
		default:
			continue;
		}
	}

	return clone_mask;
}

void rockchip_drm_encoder_setup(struct drm_device *dev)
{
	struct drm_encoder *encoder;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head)
		encoder->possible_clones = rockchip_drm_encoder_clones(encoder);
}

struct drm_encoder *
rockchip_drm_encoder_create(struct drm_device *dev,
			   struct rockchip_drm_manager *manager,
			   unsigned int possible_crtcs)
{
	struct drm_encoder *encoder;
	struct rockchip_drm_encoder *rockchip_encoder;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (!manager || !possible_crtcs)
		return NULL;

	if (!manager->dev)
		return NULL;

	rockchip_encoder = kzalloc(sizeof(*rockchip_encoder), GFP_KERNEL);
	if (!rockchip_encoder) {
		DRM_ERROR("failed to allocate encoder\n");
		return NULL;
	}

	rockchip_encoder->dpms = DRM_MODE_DPMS_OFF;
	rockchip_encoder->manager = manager;
	encoder = &rockchip_encoder->drm_encoder;
	encoder->possible_crtcs = possible_crtcs;

	DRM_DEBUG_KMS("possible_crtcs = 0x%x\n", encoder->possible_crtcs);

	drm_encoder_init(dev, encoder, &rockchip_encoder_funcs,
			DRM_MODE_ENCODER_TMDS);

	drm_encoder_helper_add(encoder, &rockchip_encoder_helper_funcs);

	DRM_DEBUG_KMS("encoder has been created\n");

	return encoder;
}

struct rockchip_drm_manager *rockchip_drm_get_manager(struct drm_encoder *encoder)
{
	return to_rockchip_encoder(encoder)->manager;
}

void rockchip_drm_fn_encoder(struct drm_crtc *crtc, void *data,
			    void (*fn)(struct drm_encoder *, void *))
{
	struct drm_device *dev = crtc->dev;
	struct drm_encoder *encoder;
	struct rockchip_drm_private *private = dev->dev_private;
	struct rockchip_drm_manager *manager;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		/*
		 * if crtc is detached from encoder, check pipe,
		 * otherwise check crtc attached to encoder
		 */
		if (!encoder->crtc) {
			manager = to_rockchip_encoder(encoder)->manager;
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
int rockchip_get_crtc_vblank_timestamp(struct drm_device *dev, int crtc,
				    int *max_error,
				    struct timeval *vblank_time,
				    unsigned flags)
{
#if 0
	ktime_t stime, etime, mono_time_offset;
	struct timeval tv_etime;
	struct drm_display_mode *mode;
	int vbl_status, vtotal, vdisplay;
	int vpos, hpos, i;
	s64 framedur_ns, linedur_ns, pixeldur_ns, delta_ns, duration_ns;
	bool invbl;

	if (crtc < 0 || crtc >= dev->num_crtcs) {
		DRM_ERROR("Invalid crtc %d\n", crtc);
		return -EINVAL;
	}

	/* Scanout position query not supported? Should not happen. */
	if (!dev->driver->get_scanout_position) {
		DRM_ERROR("Called from driver w/o get_scanout_position()!?\n");
		return -EIO;
	}
#endif
	return 0;//vbl_status;

}
void rockchip_drm_enable_vblank(struct drm_encoder *encoder, void *data)
{
	struct rockchip_drm_manager *manager =
		to_rockchip_encoder(encoder)->manager;
	struct rockchip_drm_manager_ops *manager_ops = manager->ops;
	int crtc = *(int *)data;

	if (manager->pipe != crtc)
		return;

	if (manager_ops->enable_vblank)
		manager_ops->enable_vblank(manager->dev);
}

void rockchip_drm_disable_vblank(struct drm_encoder *encoder, void *data)
{
	struct rockchip_drm_manager *manager =
		to_rockchip_encoder(encoder)->manager;
	struct rockchip_drm_manager_ops *manager_ops = manager->ops;
	int crtc = *(int *)data;

	if (manager->pipe != crtc)
		return;

	if (manager_ops->disable_vblank)
		manager_ops->disable_vblank(manager->dev);
}

void rockchip_drm_encoder_crtc_dpms(struct drm_encoder *encoder, void *data)
{
	struct rockchip_drm_encoder *rockchip_encoder = to_rockchip_encoder(encoder);
	struct rockchip_drm_manager *manager = rockchip_encoder->manager;
	struct rockchip_drm_manager_ops *manager_ops = manager->ops;
	int mode = *(int *)data;

	DRM_DEBUG_KMS("%s\n", __FILE__);

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

void rockchip_drm_encoder_crtc_pipe(struct drm_encoder *encoder, void *data)
{
	struct rockchip_drm_manager *manager =
		to_rockchip_encoder(encoder)->manager;
	int pipe = *(int *)data;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/*
	 * when crtc is detached from encoder, this pipe is used
	 * to select manager operation
	 */
	manager->pipe = pipe;
}

void rockchip_drm_encoder_plane_mode_set(struct drm_encoder *encoder, void *data)
{
	struct rockchip_drm_manager *manager =
		to_rockchip_encoder(encoder)->manager;
	struct rockchip_drm_overlay_ops *overlay_ops = manager->overlay_ops;
	struct rockchip_drm_overlay *overlay = data;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (overlay_ops && overlay_ops->mode_set)
		overlay_ops->mode_set(manager->dev, overlay);
}

void rockchip_drm_encoder_plane_commit(struct drm_encoder *encoder, void *data)
{
	struct rockchip_drm_manager *manager =
		to_rockchip_encoder(encoder)->manager;
	struct rockchip_drm_overlay_ops *overlay_ops = manager->overlay_ops;
	int zpos = DEFAULT_ZPOS;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (data)
		zpos = *(int *)data;

	if (overlay_ops && overlay_ops->commit)
		overlay_ops->commit(manager->dev, zpos);
}

void rockchip_drm_encoder_plane_enable(struct drm_encoder *encoder, void *data)
{
	struct rockchip_drm_manager *manager =
		to_rockchip_encoder(encoder)->manager;
	struct rockchip_drm_overlay_ops *overlay_ops = manager->overlay_ops;
	int zpos = DEFAULT_ZPOS;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (data)
		zpos = *(int *)data;

	if (overlay_ops && overlay_ops->enable)
		overlay_ops->enable(manager->dev, zpos);
}

void rockchip_drm_encoder_plane_disable(struct drm_encoder *encoder, void *data)
{
	struct rockchip_drm_manager *manager =
		to_rockchip_encoder(encoder)->manager;
	struct rockchip_drm_overlay_ops *overlay_ops = manager->overlay_ops;
	int zpos = DEFAULT_ZPOS;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (data)
		zpos = *(int *)data;

	if (overlay_ops && overlay_ops->disable)
		overlay_ops->disable(manager->dev, zpos);
}
