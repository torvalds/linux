// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * based on exynos_drm_drv.c
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/dma-iommu.h>
#include <linux/genalloc.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/component.h>
#include <linux/console.h>
#include <linux/iommu.h>
#include <linux/of_reserved_mem.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_fbdev.h"
#include "rockchip_drm_gem.h"
#include "rockchip_drm_logo.h"

#include "../drm_crtc_internal.h"

#define DRIVER_NAME	"rockchip"
#define DRIVER_DESC	"RockChip Soc DRM"
#define DRIVER_DATE	"20140818"
#define DRIVER_MAJOR	3
#define DRIVER_MINOR	0

static bool is_support_iommu = true;
static struct drm_driver rockchip_drm_driver;

uint32_t rockchip_drm_get_bpp(const struct drm_format_info *info)
{
	/* use whatever a driver has set */
	if (info->cpp[0])
		return info->cpp[0] * 8;

	switch (info->format) {
	case DRM_FORMAT_YUV420_8BIT:
		return 12;
	case DRM_FORMAT_YUV420_10BIT:
		return 15;
	case DRM_FORMAT_VUY101010:
		return 30;
	default:
		break;
	}

	/* all attempts failed */
	return 0;
}
EXPORT_SYMBOL(rockchip_drm_get_bpp);

/**
 * rockchip_drm_of_find_possible_crtcs - find the possible CRTCs for an active
 * encoder port
 * @dev: DRM device
 * @port: encoder port to scan for endpoints
 *
 * Scan all active endpoints attached to a port, locate their attached CRTCs,
 * and generate the DRM mask of CRTCs which may be attached to this
 * encoder.
 *
 * See Documentation/devicetree/bindings/graph.txt for the bindings.
 */
uint32_t rockchip_drm_of_find_possible_crtcs(struct drm_device *dev,
					     struct device_node *port)
{
	struct device_node *remote_port, *ep;
	uint32_t possible_crtcs = 0;

	for_each_endpoint_of_node(port, ep) {
		if (!of_device_is_available(ep))
			continue;

		remote_port = of_graph_get_remote_port(ep);
		if (!remote_port) {
			of_node_put(ep);
			return 0;
		}

		possible_crtcs |= drm_of_crtc_port_mask(dev, remote_port);

		of_node_put(remote_port);
	}

	return possible_crtcs;
}
EXPORT_SYMBOL(rockchip_drm_of_find_possible_crtcs);

static DEFINE_MUTEX(rockchip_drm_sub_dev_lock);
static LIST_HEAD(rockchip_drm_sub_dev_list);

void rockchip_drm_register_sub_dev(struct rockchip_drm_sub_dev *sub_dev)
{
	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_add_tail(&sub_dev->list, &rockchip_drm_sub_dev_list);
	mutex_unlock(&rockchip_drm_sub_dev_lock);
}
EXPORT_SYMBOL(rockchip_drm_register_sub_dev);

void rockchip_drm_unregister_sub_dev(struct rockchip_drm_sub_dev *sub_dev)
{
	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_del(&sub_dev->list);
	mutex_unlock(&rockchip_drm_sub_dev_lock);
}
EXPORT_SYMBOL(rockchip_drm_unregister_sub_dev);

struct rockchip_drm_sub_dev *rockchip_drm_get_sub_dev(struct device_node *node)
{
	struct rockchip_drm_sub_dev *sub_dev = NULL;
	bool found = false;

	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_for_each_entry(sub_dev, &rockchip_drm_sub_dev_list, list) {
		if (sub_dev->of_node == node) {
			found = true;
			break;
		}
	}
	mutex_unlock(&rockchip_drm_sub_dev_lock);

	return found ? sub_dev : NULL;
}
EXPORT_SYMBOL(rockchip_drm_get_sub_dev);

int rockchip_drm_get_sub_dev_type(void)
{
	int connector_type = DRM_MODE_CONNECTOR_Unknown;
	struct rockchip_drm_sub_dev *sub_dev = NULL;

	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_for_each_entry(sub_dev, &rockchip_drm_sub_dev_list, list) {
		if (sub_dev->connector->encoder) {
			connector_type = sub_dev->connector->connector_type;
			break;
		}
	}
	mutex_unlock(&rockchip_drm_sub_dev_lock);

	return connector_type;
}
EXPORT_SYMBOL(rockchip_drm_get_sub_dev_type);

static const struct drm_display_mode rockchip_drm_default_modes[] = {
	/* 4 - 1280x720@60Hz 16:9 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 16 - 1920x1080@60Hz 16:9 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 31 - 1920x1080@50Hz 16:9 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2448,
		   2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 19 - 1280x720@50Hz 16:9 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1720,
		   1760, 1980, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 0x10 - 1024x768@60Hz */
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 65000, 1024, 1048,
		   1184, 1344, 0,  768, 771, 777, 806, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 17 - 720x576@50Hz 4:3 */
	{ DRM_MODE("720x576", DRM_MODE_TYPE_DRIVER, 27000, 720, 732,
		   796, 864, 0, 576, 581, 586, 625, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
	/* 2 - 720x480@60Hz 4:3 */
	{ DRM_MODE("720x480", DRM_MODE_TYPE_DRIVER, 27000, 720, 736,
		   798, 858, 0, 480, 489, 495, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
};

int rockchip_drm_add_modes_noedid(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;
	int i, count, num_modes = 0;

	mutex_lock(&rockchip_drm_sub_dev_lock);
	count = ARRAY_SIZE(rockchip_drm_default_modes);

	for (i = 0; i < count; i++) {
		const struct drm_display_mode *ptr = &rockchip_drm_default_modes[i];

		mode = drm_mode_duplicate(dev, ptr);
		if (mode) {
			if (!i)
				mode->type = DRM_MODE_TYPE_PREFERRED;
			drm_mode_probed_add(connector, mode);
			num_modes++;
		}
	}
	mutex_unlock(&rockchip_drm_sub_dev_lock);

	return num_modes;
}
EXPORT_SYMBOL(rockchip_drm_add_modes_noedid);

/*
 * Attach a (component) device to the shared drm dma mapping from master drm
 * device.  This is used by the VOPs to map GEM buffers to a common DMA
 * mapping.
 */
int rockchip_drm_dma_attach_device(struct drm_device *drm_dev,
				   struct device *dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	int ret;

	if (!is_support_iommu)
		return 0;

	ret = iommu_attach_device(private->domain, dev);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to attach iommu device\n");
		return ret;
	}

	return 0;
}

void rockchip_drm_dma_detach_device(struct drm_device *drm_dev,
				    struct device *dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct iommu_domain *domain = private->domain;

	if (!is_support_iommu)
		return;

	iommu_detach_device(domain, dev);
}

int rockchip_register_crtc_funcs(struct drm_crtc *crtc,
				 const struct rockchip_crtc_funcs *crtc_funcs)
{
	int pipe = drm_crtc_index(crtc);
	struct rockchip_drm_private *priv = crtc->dev->dev_private;

	if (pipe >= ROCKCHIP_MAX_CRTC)
		return -EINVAL;

	priv->crtc_funcs[pipe] = crtc_funcs;

	return 0;
}

void rockchip_unregister_crtc_funcs(struct drm_crtc *crtc)
{
	int pipe = drm_crtc_index(crtc);
	struct rockchip_drm_private *priv = crtc->dev->dev_private;

	if (pipe >= ROCKCHIP_MAX_CRTC)
		return;

	priv->crtc_funcs[pipe] = NULL;
}

static int rockchip_drm_fault_handler(struct iommu_domain *iommu,
				      struct device *dev,
				      unsigned long iova, int flags, void *arg)
{
	struct drm_device *drm_dev = arg;
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_crtc *crtc;

	DRM_ERROR("iommu fault handler flags: 0x%x\n", flags);
	drm_for_each_crtc(crtc, drm_dev) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->regs_dump)
			priv->crtc_funcs[pipe]->regs_dump(crtc, NULL);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->debugfs_dump)
			priv->crtc_funcs[pipe]->debugfs_dump(crtc, NULL);
	}

	return 0;
}

static int rockchip_drm_init_iommu(struct drm_device *drm_dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct iommu_domain_geometry *geometry;
	u64 start, end;

	if (!is_support_iommu)
		return 0;

	private->domain = iommu_domain_alloc(&platform_bus_type);
	if (!private->domain)
		return -ENOMEM;

	geometry = &private->domain->geometry;
	start = geometry->aperture_start;
	end = geometry->aperture_end;

	DRM_DEBUG("IOMMU context initialized (aperture: %#llx-%#llx)\n",
		  start, end);
	drm_mm_init(&private->mm, start, end - start + 1);
	mutex_init(&private->mm_lock);

	iommu_set_fault_handler(private->domain, rockchip_drm_fault_handler,
				drm_dev);

	return 0;
}

static void rockchip_iommu_cleanup(struct drm_device *drm_dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;

	if (!is_support_iommu)
		return;

	drm_mm_takedown(&private->mm);
	iommu_domain_free(private->domain);
}

#ifdef CONFIG_DEBUG_FS
static int rockchip_drm_mm_dump(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *drm_dev = minor->dev;
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_printer p = drm_seq_file_printer(s);

	if (!priv->domain)
		return 0;
	mutex_lock(&priv->mm_lock);
	drm_mm_print(&priv->mm, &p);
	mutex_unlock(&priv->mm_lock);

	return 0;
}

static int rockchip_drm_summary_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *drm_dev = minor->dev;
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, drm_dev) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->debugfs_dump)
			priv->crtc_funcs[pipe]->debugfs_dump(crtc, s);
	}

	return 0;
}

static struct drm_info_list rockchip_debugfs_files[] = {
	{ "summary", rockchip_drm_summary_show, 0, NULL },
	{ "mm_dump", rockchip_drm_mm_dump, 0, NULL },
};

static void rockchip_drm_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;

	drm_debugfs_create_files(rockchip_debugfs_files,
				 ARRAY_SIZE(rockchip_debugfs_files),
				 minor->debugfs_root, minor);

	drm_for_each_crtc(crtc, dev) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->debugfs_init)
			priv->crtc_funcs[pipe]->debugfs_init(minor, crtc);
	}
}
#endif

static int rockchip_drm_create_properties(struct drm_device *dev)
{
	struct drm_property *prop;
	struct rockchip_drm_private *private = dev->dev_private;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "EOTF", 0, 5);
	if (!prop)
		return -ENOMEM;
	private->eotf_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "COLOR_SPACE", 0, 12);
	if (!prop)
		return -ENOMEM;
	private->color_space_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "GLOBAL_ALPHA", 0, 255);
	if (!prop)
		return -ENOMEM;
	private->global_alpha_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "BLEND_MODE", 0, 1);
	if (!prop)
		return -ENOMEM;
	private->blend_mode_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "ALPHA_SCALE", 0, 1);
	if (!prop)
		return -ENOMEM;
	private->alpha_scale_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "ASYNC_COMMIT", 0, 1);
	if (!prop)
		return -ENOMEM;
	private->async_commit_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "SHARE_ID", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	private->share_id_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "CONNECTOR_ID", 0, 0xf);
	if (!prop)
		return -ENOMEM;
	private->connector_id_prop = prop;

	return drm_mode_create_tv_properties(dev, 0, NULL);
}

static void rockchip_attach_connector_property(struct drm_device *drm)
{
	struct drm_connector *connector;
	struct drm_mode_config *conf = &drm->mode_config;
	struct drm_connector_list_iter conn_iter;

	mutex_lock(&drm->mode_config.mutex);

#define ROCKCHIP_PROP_ATTACH(prop, v) \
		drm_object_attach_property(&connector->base, prop, v)

	drm_connector_list_iter_begin(drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		ROCKCHIP_PROP_ATTACH(conf->tv_brightness_property, 50);
		ROCKCHIP_PROP_ATTACH(conf->tv_contrast_property, 50);
		ROCKCHIP_PROP_ATTACH(conf->tv_saturation_property, 50);
		ROCKCHIP_PROP_ATTACH(conf->tv_hue_property, 50);
	}
	drm_connector_list_iter_end(&conn_iter);
#undef ROCKCHIP_PROP_ATTACH

	mutex_unlock(&drm->mode_config.mutex);
}

static void rockchip_drm_set_property_default(struct drm_device *drm)
{
	struct drm_connector *connector;
	struct drm_mode_config *conf = &drm->mode_config;
	struct drm_atomic_state *state;
	int ret;
	struct drm_connector_list_iter conn_iter;

	drm_modeset_lock_all(drm);

	state = drm_atomic_helper_duplicate_state(drm, conf->acquire_ctx);
	if (!state) {
		DRM_ERROR("failed to alloc atomic state\n");
		goto err_unlock;
	}
	state->acquire_ctx = conf->acquire_ctx;

	drm_connector_list_iter_begin(drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct drm_connector_state *connector_state;

		connector_state = drm_atomic_get_connector_state(state,
								 connector);
		if (IS_ERR(connector_state)) {
			DRM_ERROR("Connector[%d]: Failed to get state\n", connector->base.id);
			continue;
		}

		connector_state->tv.brightness = 50;
		connector_state->tv.contrast = 50;
		connector_state->tv.saturation = 50;
		connector_state->tv.hue = 50;
	}
	drm_connector_list_iter_end(&conn_iter);

	ret = drm_atomic_commit(state);
	WARN_ON(ret == -EDEADLK);
	if (ret)
		DRM_ERROR("Failed to update properties\n");

err_unlock:
	drm_modeset_unlock_all(drm);
}

static int rockchip_gem_pool_init(struct drm_device *drm)
{
	struct rockchip_drm_private *private = drm->dev_private;
	struct device_node *np = drm->dev->of_node;
	struct device_node *node;
	phys_addr_t start, size;
	struct resource res;
	int ret;

	node = of_parse_phandle(np, "secure-memory-region", 0);
	if (!node)
		return -ENXIO;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return ret;
	start = res.start;
	size = resource_size(&res);
	if (!size)
		return -ENOMEM;

	private->secure_buffer_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!private->secure_buffer_pool)
		return -ENOMEM;

	gen_pool_add(private->secure_buffer_pool, start, size, -1);

	return 0;
}

static void rockchip_gem_pool_destroy(struct drm_device *drm)
{
	struct rockchip_drm_private *private = drm->dev_private;

	if (!private->secure_buffer_pool)
		return;

	gen_pool_destroy(private->secure_buffer_pool);
}

static int rockchip_drm_bind(struct device *dev)
{
	struct drm_device *drm_dev;
	struct rockchip_drm_private *private;
	int ret;

	drm_dev = drm_dev_alloc(&rockchip_drm_driver, dev);
	if (IS_ERR(drm_dev))
		return PTR_ERR(drm_dev);

	dev_set_drvdata(dev, drm_dev);

	private = devm_kzalloc(drm_dev->dev, sizeof(*private), GFP_KERNEL);
	if (!private) {
		ret = -ENOMEM;
		goto err_free;
	}

	mutex_init(&private->ovl_lock);

	drm_dev->dev_private = private;

	INIT_LIST_HEAD(&private->psr_list);
	mutex_init(&private->psr_list_lock);
	mutex_init(&private->commit_lock);

	ret = rockchip_drm_init_iommu(drm_dev);
	if (ret)
		goto err_free;

	ret = drmm_mode_config_init(drm_dev);
	if (ret)
		goto err_iommu_cleanup;

	rockchip_drm_mode_config_init(drm_dev);
	rockchip_drm_create_properties(drm_dev);
	/* Try to bind all sub drivers. */
	ret = component_bind_all(dev, drm_dev);
	if (ret)
		goto err_iommu_cleanup;

	rockchip_attach_connector_property(drm_dev);
	ret = drm_vblank_init(drm_dev, drm_dev->mode_config.num_crtc);
	if (ret)
		goto err_unbind_all;

	drm_mode_config_reset(drm_dev);
	rockchip_drm_set_property_default(drm_dev);

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = true, we can use the vblank feature.
	 */
	drm_dev->irq_enabled = true;

	/* init kms poll for handling hpd */
	drm_kms_helper_poll_init(drm_dev);

	rockchip_gem_pool_init(drm_dev);
	ret = of_reserved_mem_device_init(drm_dev->dev);
	if (ret)
		DRM_DEBUG_KMS("No reserved memory region assign to drm\n");

	rockchip_drm_show_logo(drm_dev);

	ret = rockchip_drm_fbdev_init(drm_dev);
	if (ret)
		goto err_unbind_all;

	drm_dev->mode_config.allow_fb_modifiers = true;

	ret = drm_dev_register(drm_dev, 0);
	if (ret)
		goto err_kms_helper_poll_fini;

	return 0;
err_kms_helper_poll_fini:
	rockchip_gem_pool_destroy(drm_dev);
	drm_kms_helper_poll_fini(drm_dev);
	rockchip_drm_fbdev_fini(drm_dev);
err_unbind_all:
	component_unbind_all(dev, drm_dev);
err_iommu_cleanup:
	rockchip_iommu_cleanup(drm_dev);
err_free:
	drm_dev_put(drm_dev);
	return ret;
}

static void rockchip_drm_unbind(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	drm_dev_unregister(drm_dev);

	rockchip_drm_fbdev_fini(drm_dev);
	rockchip_gem_pool_destroy(drm_dev);
	drm_kms_helper_poll_fini(drm_dev);

	drm_atomic_helper_shutdown(drm_dev);
	component_unbind_all(dev, drm_dev);
	rockchip_iommu_cleanup(drm_dev);

	drm_dev_put(drm_dev);
}

static void rockchip_drm_crtc_cancel_pending_vblank(struct drm_crtc *crtc,
						    struct drm_file *file_priv)
{
	struct rockchip_drm_private *priv = crtc->dev->dev_private;
	int pipe = drm_crtc_index(crtc);

	if (pipe < ROCKCHIP_MAX_CRTC &&
	    priv->crtc_funcs[pipe] &&
	    priv->crtc_funcs[pipe]->cancel_pending_vblank)
		priv->crtc_funcs[pipe]->cancel_pending_vblank(crtc, file_priv);
}

static int rockchip_drm_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, dev)
		crtc->primary->fb = NULL;

	return 0;
}

static void rockchip_drm_postclose(struct drm_device *dev,
				   struct drm_file *file_priv)
{
	struct drm_crtc *crtc;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		rockchip_drm_crtc_cancel_pending_vblank(crtc, file_priv);
}

static void rockchip_drm_lastclose(struct drm_device *dev)
{
#if 0 /* todo */
	struct rockchip_drm_private *priv = dev->dev_private;

	if (!priv->logo)
		drm_fb_helper_restore_fbdev_mode_unlocked(priv->fbdev_helper);
#endif
}

static struct drm_pending_vblank_event *
rockchip_drm_add_vcnt_event(struct drm_crtc *crtc, struct drm_file *file_priv)
{
	struct drm_pending_vblank_event *e;
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return NULL;

	e->pipe = drm_crtc_index(crtc);
	e->event.base.type = DRM_EVENT_ROCKCHIP_CRTC_VCNT;
	e->event.base.length = sizeof(e->event.vbl);
	e->event.vbl.crtc_id = crtc->base.id;
	/* store crtc pipe id */
	e->event.vbl.user_data = e->pipe;

	spin_lock_irqsave(&dev->event_lock, flags);
	drm_event_reserve_init_locked(dev, file_priv, &e->base, &e->event.base);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	return e;
}

static int rockchip_drm_get_vcnt_event_ioctl(struct drm_device *dev, void *data,
					     struct drm_file *file_priv)
{
	struct rockchip_drm_private *priv = dev->dev_private;
	union drm_wait_vblank *vblwait = data;
	struct drm_pending_vblank_event *e;
	struct drm_crtc *crtc;
	unsigned int flags, pipe;

	flags = vblwait->request.type & (_DRM_VBLANK_FLAGS_MASK | _DRM_ROCKCHIP_VCNT_EVENT);
	pipe = (vblwait->request.type & _DRM_VBLANK_HIGH_CRTC_MASK);
	if (pipe)
		pipe = pipe >> _DRM_VBLANK_HIGH_CRTC_SHIFT;
	else
		pipe = flags & _DRM_VBLANK_SECONDARY ? 1 : 0;

	crtc = drm_crtc_from_index(dev, pipe);

	if (flags & _DRM_ROCKCHIP_VCNT_EVENT) {
		e = rockchip_drm_add_vcnt_event(crtc, file_priv);
		priv->vcnt[pipe].event = e;
	}

	return 0;
}

static const struct drm_ioctl_desc rockchip_ioctls[] = {
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_CREATE, rockchip_gem_create_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_MAP_OFFSET,
			  rockchip_gem_map_offset_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_GET_PHYS, rockchip_gem_get_phys_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GET_VCNT_EVENT, rockchip_drm_get_vcnt_event_ioctl,
			  DRM_UNLOCKED),
};

static const struct file_operations rockchip_drm_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.mmap = rockchip_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.unlocked_ioctl = drm_ioctl,
	.compat_ioctl = drm_compat_ioctl,
	.release = drm_release,
};

static int rockchip_drm_gem_dmabuf_begin_cpu_access(struct dma_buf *dma_buf,
						    enum dma_data_direction dir)
{
	struct drm_gem_object *obj = dma_buf->priv;

	return rockchip_gem_prime_begin_cpu_access(obj, dir);
}

static int rockchip_drm_gem_dmabuf_end_cpu_access(struct dma_buf *dma_buf,
						  enum dma_data_direction dir)
{
	struct drm_gem_object *obj = dma_buf->priv;

	return rockchip_gem_prime_end_cpu_access(obj, dir);
}

static int rockchip_drm_gem_begin_cpu_access_partial(
	struct dma_buf *dma_buf,
	enum dma_data_direction dir,
	unsigned int offset, unsigned int len)
{
	struct drm_gem_object *obj = dma_buf->priv;

	return rockchip_gem_prime_begin_cpu_access_partial(obj, dir, offset, len);
}

static int rockchip_drm_gem_end_cpu_access_partial(
	struct dma_buf *dma_buf,
	enum dma_data_direction dir,
	unsigned int offset, unsigned int len)
{
	struct drm_gem_object *obj = dma_buf->priv;

	return rockchip_gem_prime_end_cpu_access_partial(obj, dir, offset, len);
}

static const struct dma_buf_ops rockchip_drm_gem_prime_dmabuf_ops = {
	.cache_sgt_mapping = true,
	.attach = drm_gem_map_attach,
	.detach = drm_gem_map_detach,
	.map_dma_buf = drm_gem_map_dma_buf,
	.unmap_dma_buf = drm_gem_unmap_dma_buf,
	.release = drm_gem_dmabuf_release,
	.mmap = drm_gem_dmabuf_mmap,
	.vmap = drm_gem_dmabuf_vmap,
	.vunmap = drm_gem_dmabuf_vunmap,
	.get_uuid = drm_gem_dmabuf_get_uuid,
	.begin_cpu_access = rockchip_drm_gem_dmabuf_begin_cpu_access,
	.end_cpu_access = rockchip_drm_gem_dmabuf_end_cpu_access,
	.begin_cpu_access_partial = rockchip_drm_gem_begin_cpu_access_partial,
	.end_cpu_access_partial = rockchip_drm_gem_end_cpu_access_partial,
};

static struct drm_gem_object *rockchip_drm_gem_prime_import_dev(struct drm_device *dev,
								struct dma_buf *dma_buf,
								struct device *attach_dev)
{
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct drm_gem_object *obj;
	int ret;

	if (dma_buf->ops == &rockchip_drm_gem_prime_dmabuf_ops) {
		obj = dma_buf->priv;
		if (obj->dev == dev) {
			/*
			 * Importing dmabuf exported from out own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			drm_gem_object_get(obj);
			return obj;
		}
	}

	if (!dev->driver->gem_prime_import_sg_table)
		return ERR_PTR(-EINVAL);

	attach = dma_buf_attach(dma_buf, attach_dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

	get_dma_buf(dma_buf);

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto fail_detach;
	}

	obj = dev->driver->gem_prime_import_sg_table(dev, attach, sgt);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto fail_unmap;
	}

	obj->import_attach = attach;
	obj->resv = dma_buf->resv;

	return obj;

fail_unmap:
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(dma_buf, attach);
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}

static struct drm_gem_object *rockchip_drm_gem_prime_import(struct drm_device *dev,
							    struct dma_buf *dma_buf)
{
	return rockchip_drm_gem_prime_import_dev(dev, dma_buf, dev->dev);
}

static struct dma_buf *rockchip_drm_gem_prime_export(struct drm_gem_object *obj,
						     int flags)
{
	struct drm_device *dev = obj->dev;
	struct dma_buf_export_info exp_info = {
		.exp_name = KBUILD_MODNAME, /* white lie for debug */
		.owner = dev->driver->fops->owner,
		.ops = &rockchip_drm_gem_prime_dmabuf_ops,
		.size = obj->size,
		.flags = flags,
		.priv = obj,
		.resv = obj->resv,
	};

	return drm_gem_dmabuf_export(dev, &exp_info);
}

static struct drm_driver rockchip_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.postclose		= rockchip_drm_postclose,
	.lastclose		= rockchip_drm_lastclose,
	.open			= rockchip_drm_open,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.gem_free_object_unlocked = rockchip_gem_free_object,
	.dumb_create		= rockchip_gem_dumb_create,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= rockchip_drm_gem_prime_import,
	.gem_prime_export	= rockchip_drm_gem_prime_export,
	.gem_prime_get_sg_table	= rockchip_gem_prime_get_sg_table,
	.gem_prime_import_sg_table	= rockchip_gem_prime_import_sg_table,
	.gem_prime_vmap		= rockchip_gem_prime_vmap,
	.gem_prime_vunmap	= rockchip_gem_prime_vunmap,
	.gem_prime_mmap		= rockchip_gem_mmap_buf,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init		= rockchip_drm_debugfs_init,
#endif
	.ioctls			= rockchip_ioctls,
	.num_ioctls		= ARRAY_SIZE(rockchip_ioctls),
	.fops			= &rockchip_drm_driver_fops,
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.date	= DRIVER_DATE,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
};

#ifdef CONFIG_PM_SLEEP
static int rockchip_drm_sys_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm);
}

static int rockchip_drm_sys_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(drm);
}
#endif

static const struct dev_pm_ops rockchip_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_drm_sys_suspend,
				rockchip_drm_sys_resume)
};

#define MAX_ROCKCHIP_SUB_DRIVERS 16
static struct platform_driver *rockchip_sub_drivers[MAX_ROCKCHIP_SUB_DRIVERS];
static int num_rockchip_sub_drivers;

/*
 * Check if a vop endpoint is leading to a rockchip subdriver or bridge.
 * Should be called from the component bind stage of the drivers
 * to ensure that all subdrivers are probed.
 *
 * @ep: endpoint of a rockchip vop
 *
 * returns true if subdriver, false if external bridge and -ENODEV
 * if remote port does not contain a device.
 */
int rockchip_drm_endpoint_is_subdriver(struct device_node *ep)
{
	struct device_node *node = of_graph_get_remote_port_parent(ep);
	struct platform_device *pdev;
	struct device_driver *drv;
	int i;

	if (!node)
		return -ENODEV;

	/* status disabled will prevent creation of platform-devices */
	pdev = of_find_device_by_node(node);
	of_node_put(node);
	if (!pdev)
		return -ENODEV;

	/*
	 * All rockchip subdrivers have probed at this point, so
	 * any device not having a driver now is an external bridge.
	 */
	drv = pdev->dev.driver;
	if (!drv) {
		platform_device_put(pdev);
		return false;
	}

	for (i = 0; i < num_rockchip_sub_drivers; i++) {
		if (rockchip_sub_drivers[i] == to_platform_driver(drv)) {
			platform_device_put(pdev);
			return true;
		}
	}

	platform_device_put(pdev);
	return false;
}

static int compare_dev(struct device *dev, void *data)
{
	return dev == (struct device *)data;
}

static void rockchip_drm_match_remove(struct device *dev)
{
	struct device_link *link;

	list_for_each_entry(link, &dev->links.consumers, s_node)
		device_link_del(link);
}

static struct component_match *rockchip_drm_match_add(struct device *dev)
{
	struct component_match *match = NULL;
	int i;

	for (i = 0; i < num_rockchip_sub_drivers; i++) {
		struct platform_driver *drv = rockchip_sub_drivers[i];
		struct device *p = NULL, *d;

		do {
			d = platform_find_device_by_driver(p, &drv->driver);
			put_device(p);
			p = d;

			if (!d)
				break;

			device_link_add(dev, d, DL_FLAG_STATELESS);
			component_match_add(dev, &match, compare_dev, d);
		} while (true);
	}

	if (IS_ERR(match))
		rockchip_drm_match_remove(dev);

	return match ?: ERR_PTR(-ENODEV);
}

static const struct component_master_ops rockchip_drm_ops = {
	.bind = rockchip_drm_bind,
	.unbind = rockchip_drm_unbind,
};

static int rockchip_drm_platform_of_probe(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *port;
	bool found = false;
	int i;

	if (!np)
		return -ENODEV;

	for (i = 0;; i++) {
		struct device_node *iommu;

		port = of_parse_phandle(np, "ports", i);
		if (!port)
			break;

		if (!of_device_is_available(port->parent)) {
			of_node_put(port);
			continue;
		}

		iommu = of_parse_phandle(port->parent, "iommus", 0);
		if (!iommu || !of_device_is_available(iommu)) {
			DRM_DEV_DEBUG(dev,
				      "no iommu attached for %pOF, using non-iommu buffers\n",
				      port->parent);
			/*
			 * if there is a crtc not support iommu, force set all
			 * crtc use non-iommu buffer.
			 */
			is_support_iommu = false;
		}

		found = true;

		of_node_put(iommu);
		of_node_put(port);
	}

	if (i == 0) {
		DRM_DEV_ERROR(dev, "missing 'ports' property\n");
		return -ENODEV;
	}

	if (!found) {
		DRM_DEV_ERROR(dev,
			      "No available vop found for display-subsystem.\n");
		return -ENODEV;
	}

	return 0;
}

static int rockchip_drm_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct component_match *match = NULL;
	int ret;

	ret = rockchip_drm_platform_of_probe(dev);
	if (ret)
		return ret;

	match = rockchip_drm_match_add(dev);
	if (IS_ERR(match))
		return PTR_ERR(match);

	ret = component_master_add_with_match(dev, &rockchip_drm_ops, match);
	if (ret < 0) {
		rockchip_drm_match_remove(dev);
		return ret;
	}
	dev->coherent_dma_mask = DMA_BIT_MASK(64);

	return 0;
}

static int rockchip_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &rockchip_drm_ops);

	rockchip_drm_match_remove(&pdev->dev);

	return 0;
}

static void rockchip_drm_platform_shutdown(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	if (drm)
		drm_atomic_helper_shutdown(drm);
}

static const struct of_device_id rockchip_drm_dt_ids[] = {
	{ .compatible = "rockchip,display-subsystem", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rockchip_drm_dt_ids);

static struct platform_driver rockchip_drm_platform_driver = {
	.probe = rockchip_drm_platform_probe,
	.remove = rockchip_drm_platform_remove,
	.shutdown = rockchip_drm_platform_shutdown,
	.driver = {
		.name = "rockchip-drm",
		.of_match_table = rockchip_drm_dt_ids,
		.pm = &rockchip_drm_pm_ops,
	},
};

#define ADD_ROCKCHIP_SUB_DRIVER(drv, cond) { \
	if (IS_ENABLED(cond) && \
	    !WARN_ON(num_rockchip_sub_drivers >= MAX_ROCKCHIP_SUB_DRIVERS)) \
		rockchip_sub_drivers[num_rockchip_sub_drivers++] = &drv; \
}

static int __init rockchip_drm_init(void)
{
	int ret;

	num_rockchip_sub_drivers = 0;
	ADD_ROCKCHIP_SUB_DRIVER(vop_platform_driver, CONFIG_DRM_ROCKCHIP);
	ADD_ROCKCHIP_SUB_DRIVER(vop2_platform_driver, CONFIG_DRM_ROCKCHIP);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_lvds_driver,
				CONFIG_ROCKCHIP_LVDS);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_dp_driver,
				CONFIG_ROCKCHIP_ANALOGIX_DP);
	ADD_ROCKCHIP_SUB_DRIVER(cdn_dp_driver, CONFIG_ROCKCHIP_CDN_DP);
	ADD_ROCKCHIP_SUB_DRIVER(dw_hdmi_rockchip_pltfm_driver,
				CONFIG_ROCKCHIP_DW_HDMI);
	ADD_ROCKCHIP_SUB_DRIVER(dw_mipi_dsi_rockchip_driver,
				CONFIG_ROCKCHIP_DW_MIPI_DSI);
	ADD_ROCKCHIP_SUB_DRIVER(inno_hdmi_driver, CONFIG_ROCKCHIP_INNO_HDMI);
	ADD_ROCKCHIP_SUB_DRIVER(rk3066_hdmi_driver,
				CONFIG_ROCKCHIP_RK3066_HDMI);

	ret = platform_register_drivers(rockchip_sub_drivers,
					num_rockchip_sub_drivers);
	if (ret)
		return ret;

	ret = platform_driver_register(&rockchip_drm_platform_driver);
	if (ret)
		goto err_unreg_drivers;

	return 0;

err_unreg_drivers:
	platform_unregister_drivers(rockchip_sub_drivers,
				    num_rockchip_sub_drivers);
	return ret;
}

static void __exit rockchip_drm_fini(void)
{
	platform_driver_unregister(&rockchip_drm_platform_driver);

	platform_unregister_drivers(rockchip_sub_drivers,
				    num_rockchip_sub_drivers);
}

module_init(rockchip_drm_init);
module_exit(rockchip_drm_fini);

MODULE_AUTHOR("Mark Yao <mark.yao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP DRM Driver");
MODULE_LICENSE("GPL v2");
