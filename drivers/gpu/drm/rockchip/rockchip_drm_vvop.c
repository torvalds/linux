// SPDX-License-Identifier: GPL-2.0+
/*
 * Virtual vop driver based on vkms
 *
 */

#include <linux/module.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#define DRIVER_NAME	"virtual-vop"

#define XRES_MIN    32
#define YRES_MIN    32

#define XRES_DEF  1024
#define YRES_DEF   768

#define XRES_MAX  8192
#define YRES_MAX  8192


struct vvop {
	struct device *dev;
	struct drm_device *drm_dev;
	struct platform_device *pdev;
	struct drm_crtc crtc;
	struct drm_plane *plane;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct hrtimer vblank_hrtimer;
	ktime_t period_ns;
	struct drm_pending_vblank_event *event;

};

static const u32 vvop_formats[] = {
	DRM_FORMAT_XRGB8888,
};

#define drm_crtc_to_vvop(crtc) \
	container_of(crtc, struct vvop, crtc)


static const struct drm_plane_funcs vvop_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static void vvop_plane_atomic_update(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
}

static const struct drm_plane_helper_funcs vvop_plane_helper_funcs = {
	.atomic_update		= vvop_plane_atomic_update,
};

static struct drm_plane *vvop_plane_init(struct vvop *vvop)
{
	struct drm_device *dev = vvop->drm_dev;
	struct drm_plane *plane;
	const u32 *formats;
	int ret, nformats;

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	formats = vvop_formats;
	nformats = ARRAY_SIZE(vvop_formats);

	ret = drm_universal_plane_init(dev, plane, 0,
				       &vvop_plane_funcs,
				       formats, nformats,
				       NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		kfree(plane);
		return ERR_PTR(ret);
	}

	drm_plane_helper_add(plane, &vvop_plane_helper_funcs);

	return plane;
}

static enum hrtimer_restart vvop_vblank_simulate(struct hrtimer *timer)
{
	struct vvop *vvop = container_of(timer, struct vvop, vblank_hrtimer);
	struct drm_crtc *crtc = &vvop->crtc;
	bool ret;

	ret = drm_crtc_handle_vblank(crtc);
	if (!ret)
		DRM_ERROR("vvop failure on handling vblank");

	hrtimer_forward_now(&vvop->vblank_hrtimer, vvop->period_ns);

	return HRTIMER_RESTART;
}

static int vvop_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	struct vvop *vvop = drm_crtc_to_vvop(crtc);

	drm_calc_timestamping_constants(crtc, &crtc->mode);

	hrtimer_init(&vvop->vblank_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vvop->vblank_hrtimer.function = &vvop_vblank_simulate;
	vvop->period_ns = ktime_set(0, vblank->framedur_ns);
	hrtimer_start(&vvop->vblank_hrtimer, vvop->period_ns, HRTIMER_MODE_REL);

	return 0;
}

static void vvop_disable_vblank(struct drm_crtc *crtc)
{
	struct vvop *vvop = drm_crtc_to_vvop(crtc);

	hrtimer_cancel(&vvop->vblank_hrtimer);
}

static void vvop_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs vvop_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = vvop_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_encoder_funcs vvop_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int vvop_conn_get_modes(struct drm_connector *connector)
{
	int count;

	count = drm_add_modes_noedid(connector, XRES_MAX, YRES_MAX);
	drm_set_preferred_mode(connector, XRES_DEF, YRES_DEF);

	return count;
}

static const struct drm_connector_helper_funcs vvop_conn_helper_funcs = {
	.get_modes    = vvop_conn_get_modes,
};

static const struct drm_crtc_funcs vvop_crtc_funcs = {
	.set_config             = drm_atomic_helper_set_config,
	.destroy                = drm_crtc_cleanup,
	.page_flip              = drm_atomic_helper_page_flip,
	.reset                  = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state   = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= vvop_enable_vblank,
	.disable_vblank		= vvop_disable_vblank,
};

static void vvop_crtc_atomic_enable(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	drm_crtc_vblank_on(crtc);
}

static void vvop_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_state)
{
	unsigned long flags;

	drm_crtc_vblank_off(crtc);
	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

		crtc->state->event = NULL;
	}

}

static void vvop_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_crtc_state)
{
	unsigned long flags;

	if (crtc->state->event) {
		spin_lock_irqsave(&crtc->dev->event_lock, flags);

		if (drm_crtc_vblank_get(crtc) != 0)
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
		else
			drm_crtc_arm_vblank_event(crtc, crtc->state->event);

		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

		crtc->state->event = NULL;
	}
}

static const struct drm_crtc_helper_funcs vvop_crtc_helper_funcs = {
	.atomic_flush	= vvop_crtc_atomic_flush,
	.atomic_enable	= vvop_crtc_atomic_enable,
	.atomic_disable	= vvop_crtc_atomic_disable,
};

static int vvop_crtc_init(struct drm_device *dev, struct drm_crtc *crtc,
		   struct drm_plane *primary, struct drm_plane *cursor)
{
	int ret;

	ret = drm_crtc_init_with_planes(dev, crtc, primary, cursor,
					&vvop_crtc_funcs, NULL);
	if (ret) {
		DRM_ERROR("Failed to init CRTC\n");
		return ret;
	}

	drm_crtc_helper_add(crtc, &vvop_crtc_helper_funcs);

	return ret;
}

static int vvop_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm_dev = data;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_plane *primary;
	struct drm_crtc *crtc;
	struct vvop *vvop;
	int ret;

	vvop = devm_kzalloc(dev, sizeof(*vvop), GFP_KERNEL);
	if (!vvop)
		return -ENOMEM;

	vvop->dev = dev;
	vvop->drm_dev = drm_dev;
	connector = &vvop->connector;
	encoder = &vvop->encoder;
	crtc = &vvop->crtc;

	dev_set_drvdata(dev, vvop);

	primary = vvop_plane_init(vvop);
	if (IS_ERR(primary))
		return PTR_ERR(primary);
	vvop->plane = primary;

	ret = vvop_crtc_init(drm_dev, crtc, primary, NULL);
	if (ret)
		goto err_crtc;

	ret = drm_connector_init(drm_dev, connector, &vvop_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret) {
		DRM_ERROR("Failed to init connector\n");
		goto err_connector;
	}

	drm_connector_helper_add(connector, &vvop_conn_helper_funcs);

	ret = drm_connector_register(connector);
	if (ret) {
		DRM_ERROR("Failed to register connector\n");
		goto err_connector_register;
	}

	ret = drm_encoder_init(drm_dev, encoder, &vvop_encoder_funcs,
			       DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret) {
		DRM_ERROR("Failed to init encoder\n");
		goto err_encoder;
	}
	encoder->possible_crtcs = 1;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret) {
		DRM_ERROR("Failed to attach connector to encoder\n");
		goto err_attach;
	}

	return 0;

err_attach:
	drm_encoder_cleanup(encoder);

err_encoder:
	drm_connector_unregister(connector);

err_connector_register:
	drm_connector_cleanup(connector);

err_connector:
	drm_crtc_cleanup(crtc);

err_crtc:
	drm_plane_cleanup(primary);

	return ret;
}

static void vvop_unbind(struct device *dev, struct device *master, void *data)
{
	struct vvop *vvop = dev_get_drvdata(dev);

	drm_plane_cleanup(vvop->plane);
	drm_connector_cleanup(&vvop->connector);
	drm_crtc_cleanup(&vvop->crtc);
}

const struct component_ops vvop_component_ops = {
	.bind = vvop_bind,
	.unbind = vvop_unbind,
};

static int vvop_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	DRM_DEV_INFO(dev, "virtual vop probe\n");

	return component_add(dev, &vvop_component_ops);
}

static int vvop_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vvop_component_ops);

	return 0;
}


struct platform_driver vvop_platform_driver = {
	.probe = vvop_probe,
	.remove = vvop_remove,
	.driver = {
		.name = DRIVER_NAME,
	},
};

static int __init vvop_init(void)
{
	struct platform_device *pdev;

	pdev = platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
	if (IS_ERR(pdev)) {
		DRM_ERROR("failed to register platform device %s\n", DRIVER_NAME);
		return PTR_ERR(pdev);
	}

	return 0;
}

static void __exit vvop_exit(void)
{
}

rootfs_initcall(vvop_init);
module_exit(vvop_exit);

MODULE_AUTHOR("Andy Yan <rock-chips@.com>");
MODULE_LICENSE("GPL");
