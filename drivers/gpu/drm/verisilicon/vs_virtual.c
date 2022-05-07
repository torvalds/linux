// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/component.h>
#include <linux/of_platform.h>
#include <linux/media-bus-format.h>

#include <drm/drm_probe_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_encoder.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_atomic_helper.h>

#include "vs_virtual.h"
#include "vs_dc.h"
#include "vs_gem.h"

static unsigned char __get_bpp(struct vs_virtual_display *vd)
{
	if (vd->bus_format == MEDIA_BUS_FMT_RGB101010_1X30)
		return 10;
	return 8;
}

static void vd_dump_destroy(struct vs_virtual_display *vd)
{
	if (vd->dump_blob.data) {
		vunmap(vd->dump_blob.data);
		vd->dump_blob.data = NULL;
	}
	vd->dump_blob.size = 0;

	debugfs_remove(vd->dump_debugfs);
	vd->dump_debugfs = NULL;

	if (vd->dump_obj) {
#if KERNEL_VERSION(5, 9, 0) <= LINUX_VERSION_CODE
		drm_gem_object_put(&vd->dump_obj->base);
#else
		drm_gem_object_put_unlocked(&vd->dump_obj->base);
#endif
		vd->dump_obj = NULL;
	}
}

static void vd_dump_create(struct vs_virtual_display *vd,
			   struct drm_display_mode *mode)
{
	struct drm_device *drm_dev = vd->encoder.dev;
	struct vs_dc *dc = dev_get_drvdata(vd->dc);
	struct vs_gem_object *obj;
	unsigned int pitch, size;
	void *kvaddr;
	char *name;

	if (!dc->funcs)
		return;

	vd_dump_destroy(vd);

	/* dump in 4bytes XRGB format */
	pitch = mode->hdisplay * 4;
	pitch = ALIGN(pitch, dc->hw.info->pitch_alignment);
	size = PAGE_ALIGN(pitch * mode->vdisplay);

	obj = vs_gem_create_object(drm_dev, size);
	if (IS_ERR(obj))
		return;

	vd->dump_obj = obj;
	vd->pitch = pitch;

	kvaddr = vmap(obj->pages, obj->size >> PAGE_SHIFT, VM_MAP,
			  pgprot_writecombine(PAGE_KERNEL));
	if (!kvaddr)
		goto err;

	vd->dump_blob.data = kvaddr;
	vd->dump_blob.size = obj->size;

	name = kasprintf(GFP_KERNEL, "%dx%d-XRGB-%d.raw",
			 mode->hdisplay, mode->vdisplay,
			 __get_bpp(vd));
	if (!name)
		goto err;

	vd->dump_debugfs = debugfs_create_blob(name, 0444,
					vd->connector.debugfs_entry,
					&vd->dump_blob);
	kfree(name);

	return;

err:
	vd_dump_destroy(vd);

}

static void vd_encoder_destroy(struct drm_encoder *encoder)
{
	struct vs_virtual_display *vd;

	drm_encoder_cleanup(encoder);
	vd = to_virtual_display_with_encoder(encoder);
	vd_dump_destroy(vd);
}

static const struct drm_encoder_funcs vd_encoder_funcs = {
	.destroy = vd_encoder_destroy
};

static void vd_mode_set(struct drm_encoder *encoder,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct vs_virtual_display *vd;

	vd = to_virtual_display_with_encoder(encoder);
	vd_dump_create(vd, adjusted_mode);
}

static void vd_encoder_disable(struct drm_encoder *encoder)
{
	struct vs_virtual_display *vd;
	struct vs_dc *dc;

	vd = to_virtual_display_with_encoder(encoder);
	dc = dev_get_drvdata(vd->dc);
	if (dc->funcs && dc->funcs->dump_disable)
		dc->funcs->dump_disable(vd->dc);
}

static void vd_encoder_enable(struct drm_encoder *encoder)
{
	struct vs_virtual_display *vd;
	struct vs_dc *dc;

	vd = to_virtual_display_with_encoder(encoder);
	dc = dev_get_drvdata(vd->dc);
	if (dc->funcs && dc->funcs->dump_enable && vd->dump_obj)
		dc->funcs->dump_enable(vd->dc, vd->dump_obj->iova,
					  vd->pitch);
}

static const struct drm_encoder_helper_funcs vd_encoder_helper_funcs = {
	.mode_set = vd_mode_set,
	.enable = vd_encoder_enable,
	.disable = vd_encoder_disable,
};

static int vd_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode = NULL;
	unsigned int i;
	static const struct display_mode {
		int w, h;
	} cvt_mode[] = {
		{640, 480},
		{720, 480},
		{800, 600},
		{1024, 768},
		{1280, 720},
		{1280, 1024},
		{1400, 1050},
		{1680, 1050},
		{1600, 1200},
		{1920, 1080},
		{1920, 1200},
		{3840, 2160}
	};

	for (i = 0; i < ARRAY_SIZE(cvt_mode); i++) {
		mode = drm_cvt_mode(dev, cvt_mode[i].w, cvt_mode[i].h,
					60, false, false, false);
		drm_mode_probed_add(connector, mode);
	}
	return 0;
}

static struct drm_encoder *vd_best_encoder(struct drm_connector *connector)
{
	struct vs_virtual_display *vd;

	vd = to_virtual_display_with_connector(connector);
	return &vd->encoder;
}

static enum drm_mode_status vd_mode_valid(struct drm_connector *connector,
					  struct drm_display_mode *mode)
{
	return MODE_OK;
}

static const struct drm_connector_helper_funcs vd_connector_helper_funcs = {
	.get_modes = vd_get_modes,
	.mode_valid = vd_mode_valid,
	.best_encoder = vd_best_encoder,
};

static void vd_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static enum drm_connector_status
vd_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs vd_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = vd_connector_destroy,
	.detect = vd_connector_detect,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.reset = drm_atomic_helper_connector_reset,
};

static int vd_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm_dev = data;
	struct vs_virtual_display *vd = dev_get_drvdata(dev);
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct device_node *ep, *np;
	struct platform_device *pdev;
	int ret;

	/* Encoder */
	encoder = &vd->encoder;
	ret = drm_encoder_init(drm_dev, encoder, &vd_encoder_funcs,
				   DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret)
		return ret;
	drm_encoder_helper_add(encoder, &vd_encoder_helper_funcs);

	encoder->possible_crtcs =
			drm_of_find_possible_crtcs(drm_dev, dev->of_node);

	/* Connector */
	connector = &vd->connector;
	ret = drm_connector_init(drm_dev, connector, &vd_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret)
		goto connector_init_err;
	drm_connector_helper_add(connector, &vd_connector_helper_funcs);
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;
	connector->dpms = DRM_MODE_DPMS_OFF;
	connector->polled = DRM_CONNECTOR_POLL_CONNECT |
				DRM_CONNECTOR_POLL_DISCONNECT;
	ret = drm_connector_register(connector);
	if (ret)
		goto connector_reg_err;

	drm_display_info_set_bus_formats(&connector->display_info,
					 &vd->bus_format, 1);

	/* attach */
	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		goto attach_err;

	ep = of_graph_get_endpoint_by_regs(dev->of_node, 0, -1);
	if (!ep) {
		ret = -EINVAL;
		goto attach_err;
	}

	np = of_graph_get_remote_port_parent(ep);
	of_node_put(ep);
	if (!np) {
		ret = -EINVAL;
		goto attach_err;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		ret = -EPROBE_DEFER;
		goto attach_err;
	}
	get_device(&pdev->dev);
	vd->dc = &pdev->dev;

	return 0;

attach_err:
	drm_connector_unregister(connector);
connector_reg_err:
	drm_connector_cleanup(connector);
connector_init_err:
	drm_encoder_cleanup(encoder);
	return ret;
}

static void vd_unbind(struct device *dev, struct device *master, void *data)
{
	struct vs_virtual_display *vd = dev_get_drvdata(dev);

	drm_connector_unregister(&vd->connector);
	drm_connector_cleanup(&vd->connector);
	drm_encoder_cleanup(&vd->encoder);
}

const struct component_ops vd_component_ops = {
	.bind = vd_bind,
	.unbind = vd_unbind,
};

static const struct of_device_id vd_driver_dt_match[] = {
	{ .compatible = "verisilicon,virtual_display", },
	{},
};
MODULE_DEVICE_TABLE(of, vd_driver_dt_match);

static int vd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vs_virtual_display *vd;
	unsigned char bpp;

	vd = devm_kzalloc(dev, sizeof(*vd), GFP_KERNEL);
	if (!vd)
		return -ENOMEM;

	vd->bus_format = MEDIA_BUS_FMT_RGB101010_1X30;
	of_property_read_u8(dev->of_node, "bpp", &bpp);
	if (bpp == 8)
		vd->bus_format = MEDIA_BUS_FMT_RBG888_1X24;

	dev_set_drvdata(dev, vd);

	return component_add(dev, &vd_component_ops);
}

static int vd_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	component_del(dev, &vd_component_ops);

	dev_set_drvdata(dev, NULL);

	return 0;
}

struct platform_driver virtual_display_platform_driver = {
	.probe = vd_probe,
	.remove = vd_remove,
	.driver = {
		.name = "vs-virtual-display",
		.of_match_table = of_match_ptr(vd_driver_dt_match),
	},
};

MODULE_DESCRIPTION("VeriSilicon Virtual Display Driver");
MODULE_LICENSE("GPL v2");
