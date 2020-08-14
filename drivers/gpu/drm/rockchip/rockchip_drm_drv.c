/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * based on exynos_drm_drv.c
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
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <linux/devfreq.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/dma-iommu.h>
#include <linux/genalloc.h>
#include <linux/pm_runtime.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/component.h>
#include <linux/console.h>
#include <linux/iommu.h>
#include <linux/of_reserved_mem.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_fbdev.h"
#include "rockchip_drm_gem.h"

#include "../drm_internal.h"

#define DRIVER_NAME	"rockchip"
#define DRIVER_DESC	"RockChip Soc DRM"
#define DRIVER_DATE	"20140818"
#define DRIVER_MAJOR	2
#define DRIVER_MINOR	0
#define DRIVER_PATCH	0

/***********************************************************************
 *  Rockchip DRM driver version
 *
 *  v2.0.0	: add basic version for linux 4.19 rockchip drm driver(hjc)
 *
 **********************************************************************/

static bool is_support_iommu = true;
static struct drm_driver rockchip_drm_driver;

struct rockchip_drm_mode_set {
	struct list_head head;
	struct drm_framebuffer *fb;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	struct drm_display_mode *mode;
	int hdisplay;
	int vdisplay;
	int vrefresh;
	int flags;
	int picture_aspect_ratio;
	int crtc_hsync_end;
	int crtc_vsync_end;

	int left_margin;
	int right_margin;
	int top_margin;
	int bottom_margin;

	bool mode_changed;
	int ratio;
};

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

	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_for_each_entry(sub_dev, &rockchip_drm_sub_dev_list, list) {
		if (sub_dev->of_node == node)
			break;
	}
	mutex_unlock(&rockchip_drm_sub_dev_lock);

	return sub_dev;
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
	  .vrefresh = 60, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 16 - 1920x1080@60Hz 16:9 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .vrefresh = 60, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 31 - 1920x1080@50Hz 16:9 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2448,
		   2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .vrefresh = 50, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 19 - 1280x720@50Hz 16:9 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1720,
		   1760, 1980, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .vrefresh = 50, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 17 - 720x576@50Hz 4:3 */
	{ DRM_MODE("720x576", DRM_MODE_TYPE_DRIVER, 27000, 720, 732,
		   796, 864, 0, 576, 581, 586, 625, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .vrefresh = 50, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
	/* 2 - 720x480@60Hz 4:3 */
	{ DRM_MODE("720x480", DRM_MODE_TYPE_DRIVER, 27000, 720, 736,
		   798, 858, 0, 480, 489, 495, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .vrefresh = 60, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
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

#ifdef CONFIG_ARCH_ROCKCHIP
struct drm_prime_callback_data {
	struct drm_gem_object *obj;
	struct sg_table *sgt;
};
#endif

#ifndef MODULE
static struct drm_crtc *find_crtc_by_node(struct drm_device *drm_dev, struct device_node *node)
{
	struct device_node *np_crtc;
	struct drm_crtc *crtc;

	np_crtc = of_get_parent(node);
	if (!np_crtc || !of_device_is_available(np_crtc))
		return NULL;

	drm_for_each_crtc(crtc, drm_dev) {
		if (crtc->port == np_crtc)
			return crtc;
	}

	return NULL;
}

static struct drm_connector *find_connector_by_node(struct drm_device *drm_dev,
						    struct device_node *node)
{
	struct device_node *np_connector;
	struct rockchip_drm_sub_dev *sub_dev;

	np_connector = of_graph_get_remote_port_parent(node);
	if (!np_connector || !of_device_is_available(np_connector))
		return NULL;

	sub_dev = rockchip_drm_get_sub_dev(np_connector);
	if (!sub_dev)
		return NULL;

	return sub_dev->connector;
}

static struct drm_connector *find_connector_by_bridge(struct drm_device *drm_dev,
						      struct device_node *node)
{
	struct device_node *np_encoder, *np_connector = NULL;
	struct drm_connector *connector = NULL;
	struct device_node *port, *endpoint;
	struct rockchip_drm_sub_dev *sub_dev;

	np_encoder = of_graph_get_remote_port_parent(node);
	if (!np_encoder || !of_device_is_available(np_encoder))
		goto err_put_encoder;

	port = of_graph_get_port_by_id(np_encoder, 1);
	if (!port) {
		dev_err(drm_dev->dev, "can't found port point!\n");
		goto err_put_encoder;
	}

	for_each_child_of_node(port, endpoint) {
		np_connector = of_graph_get_remote_port_parent(endpoint);
		if (!np_connector) {
			dev_err(drm_dev->dev,
				"can't found connector node, please init!\n");
			goto err_put_port;
		}
		if (!of_device_is_available(np_connector)) {
			of_node_put(np_connector);
			np_connector = NULL;
			continue;
		} else {
			break;
		}
	}
	if (!np_connector) {
		dev_err(drm_dev->dev, "can't found available connector node!\n");
		goto err_put_port;
	}

	sub_dev = rockchip_drm_get_sub_dev(np_connector);
	if (!sub_dev)
		goto err_put_port;
	connector = sub_dev->connector;

	of_node_put(np_connector);
err_put_port:
	of_node_put(port);
err_put_encoder:
	of_node_put(np_encoder);

	return connector;
}

void rockchip_free_loader_memory(struct drm_device *drm)
{
	struct rockchip_drm_private *private = drm->dev_private;
	struct rockchip_logo *logo;
	void *start, *end;

	if (!private || !private->logo || --private->logo->count)
		return;

	logo = private->logo;
	start = phys_to_virt(logo->dma_addr);
	end = phys_to_virt(logo->dma_addr + logo->size);

	if (private->domain) {
		u32 pg_size = 1UL << __ffs(private->domain->pgsize_bitmap);

		iommu_unmap(private->domain, logo->dma_addr, ALIGN(logo->size, pg_size));
	}

	memblock_free(logo->start, logo->size);
	free_reserved_area(start, end, -1, "drm_logo");
	kfree(logo);
	private->logo = NULL;
	private->loader_protect = false;
}

static int init_loader_memory(struct drm_device *drm_dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct rockchip_logo *logo;
	struct device_node *np = drm_dev->dev->of_node;
	struct device_node *node;
	phys_addr_t start, size;
	u32 pg_size = PAGE_SIZE;
	struct resource res;
	int ret;

	node = of_parse_phandle(np, "logo-memory-region", 0);
	if (!node)
		return -ENOMEM;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return ret;
	if (private->domain)
		pg_size = 1UL << __ffs(private->domain->pgsize_bitmap);
	start = ALIGN_DOWN(res.start, pg_size);
	size = resource_size(&res);
	if (!size)
		return -ENOMEM;

	logo = kmalloc(sizeof(*logo), GFP_KERNEL);
	if (!logo)
		return -ENOMEM;

	logo->kvaddr = phys_to_virt(start);

	if (private->domain) {
		ret = iommu_map(private->domain, start, start, ALIGN(size, pg_size),
				IOMMU_WRITE | IOMMU_READ);
		if (ret) {
			dev_err(drm_dev->dev, "failed to create 1v1 mapping\n");
			goto err_free_logo;
		}
	}

	logo->dma_addr = start;
	logo->size = size;
	logo->count = 1;
	private->logo = logo;

	return 0;

err_free_logo:
	kfree(logo);

	return ret;
}

static struct drm_framebuffer *
get_framebuffer_by_node(struct drm_device *drm_dev, struct device_node *node)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	u32 val;
	int bpp;

	if (WARN_ON(!private->logo))
		return NULL;

	if (of_property_read_u32(node, "logo,offset", &val)) {
		pr_err("%s: failed to get logo,offset\n", __func__);
		return NULL;
	}
	mode_cmd.offsets[0] = val;

	if (of_property_read_u32(node, "logo,width", &val)) {
		pr_err("%s: failed to get logo,width\n", __func__);
		return NULL;
	}
	mode_cmd.width = val;

	if (of_property_read_u32(node, "logo,height", &val)) {
		pr_err("%s: failed to get logo,height\n", __func__);
		return NULL;
	}
	mode_cmd.height = val;

	if (of_property_read_u32(node, "logo,bpp", &val)) {
		pr_err("%s: failed to get logo,bpp\n", __func__);
		return NULL;
	}
	bpp = val;

	mode_cmd.pitches[0] = ALIGN(mode_cmd.width * bpp, 32) / 8;

	switch (bpp) {
	case 16:
		mode_cmd.pixel_format = DRM_FORMAT_RGB565;
		break;
	case 24:
		mode_cmd.pixel_format = DRM_FORMAT_RGB888;
		break;
	case 32:
		mode_cmd.pixel_format = DRM_FORMAT_XRGB8888;
		break;
	default:
		pr_err("%s: unsupported to logo bpp %d\n", __func__, bpp);
		return NULL;
	}

	return rockchip_fb_alloc(drm_dev, &mode_cmd, NULL, private->logo, 1);
}

static struct rockchip_drm_mode_set *
of_parse_display_resource(struct drm_device *drm_dev, struct device_node *route)
{
	struct rockchip_drm_mode_set *set;
	struct device_node *connect;
	struct drm_framebuffer *fb;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	const char *string;
	u32 val;

	connect = of_parse_phandle(route, "connect", 0);
	if (!connect)
		return NULL;

	fb = get_framebuffer_by_node(drm_dev, route);
	if (IS_ERR_OR_NULL(fb))
		return NULL;

	crtc = find_crtc_by_node(drm_dev, connect);
	connector = find_connector_by_node(drm_dev, connect);
	if (!connector)
		connector = find_connector_by_bridge(drm_dev, connect);
	if (!crtc || !connector) {
		dev_warn(drm_dev->dev,
			 "No available crtc or connector for display");
		drm_framebuffer_put(fb);
		return NULL;
	}

	set = kzalloc(sizeof(*set), GFP_KERNEL);
	if (!set)
		return NULL;

	if (!of_property_read_u32(route, "video,hdisplay", &val))
		set->hdisplay = val;

	if (!of_property_read_u32(route, "video,vdisplay", &val))
		set->vdisplay = val;

	if (!of_property_read_u32(route, "video,crtc_hsync_end", &val))
		set->crtc_hsync_end = val;

	if (!of_property_read_u32(route, "video,crtc_vsync_end", &val))
		set->crtc_vsync_end = val;

	if (!of_property_read_u32(route, "video,vrefresh", &val))
		set->vrefresh = val;

	if (!of_property_read_u32(route, "video,flags", &val))
		set->flags = val;

	if (!of_property_read_u32(route, "video,aspect_ratio", &val))
		set->picture_aspect_ratio = val;

	if (!of_property_read_u32(route, "overscan,left_margin", &val))
		set->left_margin = val;

	if (!of_property_read_u32(route, "overscan,right_margin", &val))
		set->right_margin = val;

	if (!of_property_read_u32(route, "overscan,top_margin", &val))
		set->top_margin = val;

	if (!of_property_read_u32(route, "overscan,bottom_margin", &val))
		set->bottom_margin = val;

	set->ratio = 1;
	if (!of_property_read_string(route, "logo,mode", &string) &&
	    !strcmp(string, "fullscreen"))
		set->ratio = 0;

	set->fb = fb;
	set->crtc = crtc;
	set->connector = connector;

	return set;
}

static int rockchip_drm_fill_connector_modes(struct drm_connector *connector,
					     uint32_t maxX, uint32_t maxY)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;
	const struct drm_connector_helper_funcs *connector_funcs =
		connector->helper_private;
	int count = 0;
	bool verbose_prune = true;
	enum drm_connector_status old_status;

	WARN_ON(!mutex_is_locked(&dev->mode_config.mutex));

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n", connector->base.id,
		      connector->name);
	/* set all modes to the unverified state */
	list_for_each_entry(mode, &connector->modes, head)
		mode->status = MODE_STALE;

	if (connector->force) {
		if (connector->force == DRM_FORCE_ON ||
		    connector->force == DRM_FORCE_ON_DIGITAL)
			connector->status = connector_status_connected;
		else
			connector->status = connector_status_disconnected;
		if (connector->funcs->force)
			connector->funcs->force(connector);
	} else {
		old_status = connector->status;

		if (connector->funcs->detect)
			connector->status = connector->funcs->detect(connector, true);
		else
			connector->status  = connector_status_connected;
		/*
		 * Normally either the driver's hpd code or the poll loop should
		 * pick up any changes and fire the hotplug event. But if
		 * userspace sneaks in a probe, we might miss a change. Hence
		 * check here, and if anything changed start the hotplug code.
		 */
		if (old_status != connector->status) {
			DRM_DEBUG_KMS("[CONNECTOR:%d:%s] status updated from %d to %d\n",
				      connector->base.id,
				      connector->name,
				      old_status, connector->status);

			/*
			 * The hotplug event code might call into the fb
			 * helpers, and so expects that we do not hold any
			 * locks. Fire up the poll struct instead, it will
			 * disable itself again.
			 */
			dev->mode_config.delayed_event = true;
			if (dev->mode_config.poll_enabled)
				schedule_delayed_work(&dev->mode_config.output_poll_work,
						      0);
		}
	}

	/* Re-enable polling in case the global poll config changed. */
	if (!dev->mode_config.poll_running)
		drm_kms_helper_poll_enable(dev);

	dev->mode_config.poll_running = true;

	if (connector->status == connector_status_disconnected) {
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] disconnected\n",
			      connector->base.id, connector->name);
		drm_connector_update_edid_property(connector, NULL);
		verbose_prune = false;
		goto prune;
	}

	count = (*connector_funcs->get_modes)(connector);

	if (count == 0 && connector->status == connector_status_connected)
		count = drm_add_modes_noedid(connector, 1024, 768);
	if (count == 0)
		goto prune;

	drm_connector_list_update(connector);

	list_for_each_entry(mode, &connector->modes, head) {
		if (mode->status == MODE_OK)
			mode->status = drm_mode_validate_driver(dev, mode);

		if (mode->status == MODE_OK)
			mode->status = drm_mode_validate_size(mode, maxX, maxY);

		/**
		 * if (mode->status == MODE_OK)
		 *	mode->status = drm_mode_validate_flag(mode, mode_flags);
		 */
		if (mode->status == MODE_OK && connector_funcs->mode_valid)
			mode->status = connector_funcs->mode_valid(connector,
								   mode);
		if (mode->status == MODE_OK)
			mode->status = drm_mode_validate_ycbcr420(mode,
								  connector);
	}

prune:
	drm_mode_prune_invalid(dev, &connector->modes, verbose_prune);

	if (list_empty(&connector->modes))
		return 0;

	list_for_each_entry(mode, &connector->modes, head)
		mode->vrefresh = drm_mode_vrefresh(mode);

	drm_mode_sort(&connector->modes);

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s] probed modes :\n", connector->base.id,
		      connector->name);
	list_for_each_entry(mode, &connector->modes, head) {
		drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V);
		drm_mode_debug_printmodeline(mode);
	}

	return count;
}

static int setup_initial_state(struct drm_device *drm_dev,
			       struct drm_atomic_state *state,
			       struct rockchip_drm_mode_set *set)
{
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_connector *connector = set->connector;
	struct drm_crtc *crtc = set->crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	struct drm_plane_state *primary_state;
	struct drm_display_mode *mode = NULL;
	const struct drm_connector_helper_funcs *funcs;
	const struct drm_encoder_helper_funcs *encoder_funcs;
	int pipe = drm_crtc_index(crtc);
	bool is_crtc_enabled = true;
	int hdisplay, vdisplay;
	int fb_width, fb_height;
	int found = 0, match = 0;
	int num_modes;
	int ret = 0;
	struct rockchip_crtc_state *s = NULL;

	if (!set->hdisplay || !set->vdisplay || !set->vrefresh)
		is_crtc_enabled = false;

	conn_state = drm_atomic_get_connector_state(state, connector);
	if (IS_ERR(conn_state))
		return PTR_ERR(conn_state);

	funcs = connector->helper_private;

	if (funcs->best_encoder)
		conn_state->best_encoder = funcs->best_encoder(connector);
	else
		conn_state->best_encoder = drm_atomic_helper_best_encoder(connector);

	if (funcs->loader_protect)
		funcs->loader_protect(connector, true);
	connector->loader_protect = true;
	encoder_funcs = conn_state->best_encoder->helper_private;
	if (encoder_funcs->loader_protect)
		encoder_funcs->loader_protect(conn_state->best_encoder, true);
	conn_state->best_encoder->loader_protect = true;
	num_modes = rockchip_drm_fill_connector_modes(connector, 4096, 4096);
	if (!num_modes) {
		dev_err(drm_dev->dev, "connector[%s] can't found any modes\n",
			connector->name);
		ret = -EINVAL;
		goto error_conn;
	}

	list_for_each_entry(mode, &connector->modes, head) {
		if (mode->hdisplay == set->hdisplay &&
		    mode->vdisplay == set->vdisplay &&
		    mode->crtc_hsync_end == set->crtc_hsync_end &&
		    mode->crtc_vsync_end == set->crtc_vsync_end &&
		    drm_mode_vrefresh(mode) == set->vrefresh &&
		    /* we just need to focus on DRM_MODE_FLAG_ALL flag, so here
		     * we compare mode->flags with set->flags & DRM_MODE_FLAG_ALL.
		     */
		    mode->flags == (set->flags & DRM_MODE_FLAG_ALL) &&
		    mode->picture_aspect_ratio == set->picture_aspect_ratio) {
			found = 1;
			match = 1;
			break;
		}
	}

	if (!found) {
		ret = -EINVAL;
		connector->status = connector_status_disconnected;
		goto error_conn;
	}

	set->mode = mode;
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto error_conn;
	}

	drm_mode_copy(&crtc_state->adjusted_mode, mode);
	if (!match || !is_crtc_enabled) {
		set->mode_changed = true;
	} else {
		ret = drm_atomic_set_crtc_for_connector(conn_state, crtc);
		if (ret)
			goto error_conn;

		ret = drm_atomic_set_mode_for_crtc(crtc_state, mode);
		if (ret)
			goto error_conn;

		crtc_state->active = true;

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->loader_protect)
			priv->crtc_funcs[pipe]->loader_protect(crtc, true);
	}

	if (!set->fb) {
		ret = 0;
		goto error_crtc;
	}
	primary_state = drm_atomic_get_plane_state(state, crtc->primary);
	if (IS_ERR(primary_state)) {
		ret = PTR_ERR(primary_state);
		goto error_crtc;
	}

	hdisplay = mode->hdisplay;
	vdisplay = mode->vdisplay;
	fb_width = set->fb->width;
	fb_height = set->fb->height;

	primary_state->crtc = crtc;
	primary_state->src_x = 0;
	primary_state->src_y = 0;
	primary_state->src_w = fb_width << 16;
	primary_state->src_h = fb_height << 16;
	if (set->ratio) {
		if (set->fb->width >= hdisplay) {
			primary_state->crtc_x = 0;
			primary_state->crtc_w = hdisplay;
		} else {
			primary_state->crtc_x = (hdisplay - fb_width) / 2;
			primary_state->crtc_w = set->fb->width;
		}

		if (set->fb->height >= vdisplay) {
			primary_state->crtc_y = 0;
			primary_state->crtc_h = vdisplay;
		} else {
			primary_state->crtc_y = (vdisplay - fb_height) / 2;
			primary_state->crtc_h = fb_height;
		}
	} else {
		primary_state->crtc_x = 0;
		primary_state->crtc_y = 0;
		primary_state->crtc_w = hdisplay;
		primary_state->crtc_h = vdisplay;
	}
	s = to_rockchip_crtc_state(crtc->state);
	s->output_type = connector->connector_type;

	return 0;

error_crtc:
	if (priv->crtc_funcs[pipe] && priv->crtc_funcs[pipe]->loader_protect)
		priv->crtc_funcs[pipe]->loader_protect(crtc, false);
error_conn:
	if (funcs->loader_protect)
		funcs->loader_protect(connector, false);
	connector->loader_protect = false;
	if (encoder_funcs->loader_protect)
		encoder_funcs->loader_protect(conn_state->best_encoder, false);
	conn_state->best_encoder->loader_protect = false;

	return ret;
}

static int update_state(struct drm_device *drm_dev,
			struct drm_atomic_state *state,
			struct rockchip_drm_mode_set *set,
			unsigned int *plane_mask)
{
	struct drm_crtc *crtc = set->crtc;
	struct drm_connector *connector = set->connector;
	struct drm_display_mode *mode = set->mode;
	struct drm_plane_state *primary_state;
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	int ret;
	struct rockchip_crtc_state *s;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);
	conn_state = drm_atomic_get_connector_state(state, connector);
	if (IS_ERR(conn_state))
		return PTR_ERR(conn_state);
	s = to_rockchip_crtc_state(crtc_state);
	s->left_margin = set->left_margin;
	s->right_margin = set->right_margin;
	s->top_margin = set->top_margin;
	s->bottom_margin = set->bottom_margin;

	if (set->mode_changed) {
		ret = drm_atomic_set_crtc_for_connector(conn_state, crtc);
		if (ret)
			return ret;

		ret = drm_atomic_set_mode_for_crtc(crtc_state, mode);
		if (ret)
			return ret;

		crtc_state->active = true;
	} else {
		const struct drm_encoder_helper_funcs *encoder_helper_funcs;
		const struct drm_connector_helper_funcs *connector_helper_funcs;
		struct drm_encoder *encoder;

		connector_helper_funcs = connector->helper_private;
		if (!connector_helper_funcs)
			return -ENXIO;
		if (connector_helper_funcs->best_encoder)
			encoder = connector_helper_funcs->best_encoder(connector);
		else
			encoder = drm_atomic_helper_best_encoder(connector);
		if (!encoder)
			return -ENXIO;
		encoder_helper_funcs = encoder->helper_private;
		if (!encoder_helper_funcs->atomic_check)
			return -ENXIO;
		ret = encoder_helper_funcs->atomic_check(encoder, crtc->state,
							 conn_state);
		if (ret)
			return ret;

		if (encoder_helper_funcs->atomic_mode_set)
			encoder_helper_funcs->atomic_mode_set(encoder,
							      crtc_state,
							      conn_state);
		else if (encoder_helper_funcs->mode_set)
			encoder_helper_funcs->mode_set(encoder, mode, mode);
	}

	primary_state = drm_atomic_get_plane_state(state, crtc->primary);
	if (IS_ERR(primary_state))
		return PTR_ERR(primary_state);

	crtc_state->plane_mask = 1 << drm_plane_index(crtc->primary);
	*plane_mask |= crtc_state->plane_mask;

	drm_atomic_set_fb_for_plane(primary_state, set->fb);
	drm_framebuffer_put(set->fb);
	ret = drm_atomic_set_crtc_for_plane(primary_state, crtc);

	return ret;
}

static void show_loader_logo(struct drm_device *drm_dev)
{
	struct drm_atomic_state *state, *old_state;
	struct device_node *np = drm_dev->dev->of_node;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct device_node *root, *route;
	struct rockchip_drm_mode_set *set, *tmp, *unset;
	struct list_head mode_set_list;
	struct list_head mode_unset_list;
	unsigned int plane_mask = 0;
	int ret, i;

	root = of_get_child_by_name(np, "route");
	if (!root) {
		dev_warn(drm_dev->dev, "failed to parse display resources\n");
		return;
	}

	if (init_loader_memory(drm_dev)) {
		dev_warn(drm_dev->dev, "failed to parse loader memory\n");
		return;
	}

	INIT_LIST_HEAD(&mode_set_list);
	INIT_LIST_HEAD(&mode_unset_list);
	drm_modeset_lock_all(drm_dev);
	state = drm_atomic_state_alloc(drm_dev);
	if (!state) {
		dev_err(drm_dev->dev, "failed to alloc atomic state\n");
		ret = -ENOMEM;
		goto err_unlock;
	}

	state->acquire_ctx = mode_config->acquire_ctx;

	for_each_child_of_node(root, route) {
		if (!of_device_is_available(route))
			continue;

		set = of_parse_display_resource(drm_dev, route);
		if (!set)
			continue;

		if (setup_initial_state(drm_dev, state, set)) {
			drm_framebuffer_put(set->fb);
			INIT_LIST_HEAD(&set->head);
			list_add_tail(&set->head, &mode_unset_list);
			continue;
		}
		INIT_LIST_HEAD(&set->head);
		list_add_tail(&set->head, &mode_set_list);
	}

	/*
	 * the mode_unset_list store the unconnected route, if route's crtc
	 * isn't used, we should close it.
	 */
	list_for_each_entry_safe(unset, tmp, &mode_unset_list, head) {
		struct rockchip_drm_mode_set *tmp_set;
		int find_used_crtc = 0;

		list_for_each_entry_safe(set, tmp_set, &mode_set_list, head) {
			if (set->crtc == unset->crtc) {
				find_used_crtc = 1;
				continue;
			}
		}

		if (!find_used_crtc) {
			struct drm_crtc *crtc = unset->crtc;
			int pipe = drm_crtc_index(crtc);
			struct rockchip_drm_private *priv =
							drm_dev->dev_private;

			if (unset->hdisplay && unset->vdisplay)
				priv->crtc_funcs[pipe]->crtc_close(crtc);
		}

		list_del(&unset->head);
		kfree(unset);
	}

	if (list_empty(&mode_set_list)) {
		dev_warn(drm_dev->dev, "can't not find any loader display\n");
		ret = -ENXIO;
		goto err_free_state;
	}

	/*
	 * The state save initial devices status, swap the state into
	 * drm devices as old state, so if new state come, can compare
	 * with this state to judge which status need to update.
	 */
	WARN_ON(drm_atomic_helper_swap_state(state, false));
	drm_atomic_state_put(state);
	old_state = drm_atomic_helper_duplicate_state(drm_dev,
						      mode_config->acquire_ctx);
	if (IS_ERR(old_state)) {
		dev_err(drm_dev->dev, "failed to duplicate atomic state\n");
		ret = PTR_ERR_OR_ZERO(old_state);
		goto err_free_state;
	}

	state = drm_atomic_helper_duplicate_state(drm_dev,
						  mode_config->acquire_ctx);
	if (IS_ERR(state)) {
		dev_err(drm_dev->dev, "failed to duplicate atomic state\n");
		ret = PTR_ERR_OR_ZERO(state);
		goto err_free_old_state;
	}
	state->acquire_ctx = mode_config->acquire_ctx;
	list_for_each_entry(set, &mode_set_list, head)
		/*
		 * We don't want to see any fail on update_state.
		 */
		WARN_ON(update_state(drm_dev, state, set, &plane_mask));

	for (i = 0; i < state->num_connector; i++) {
		if (state->connectors[i].new_state->connector->status !=
		    connector_status_connected)
			state->connectors[i].new_state->best_encoder = NULL;
	}

	ret = drm_atomic_commit(state);
	/**
	 * todo
	 * drm_atomic_clean_old_fb(drm_dev, plane_mask, ret);
	 */

	list_for_each_entry_safe(set, tmp, &mode_set_list, head) {
		list_del(&set->head);
		kfree(set);
	}

	/*
	 * Is possible get deadlock here?
	 */
	WARN_ON(ret == -EDEADLK);

	if (ret) {
		/*
		 * restore display status if atomic commit failed.
		 */
		WARN_ON(drm_atomic_helper_swap_state(old_state, false));
		goto err_free_state;
	}

	rockchip_free_loader_memory(drm_dev);
	drm_atomic_state_put(old_state);
	drm_atomic_state_put(state);

	private->loader_protect = true;
	drm_modeset_unlock_all(drm_dev);
	return;
err_free_old_state:
	drm_atomic_state_put(old_state);
err_free_state:
	drm_atomic_state_put(state);
err_unlock:
	drm_modeset_unlock_all(drm_dev);
	if (ret)
		dev_err(drm_dev->dev, "failed to show loader logo\n");
}

static const char *const loader_protect_clocks[] __initconst = {
	"hclk_vio",
	"hclk_vop",
	"hclk_vopb",
	"hclk_vopl",
	"aclk_vio",
	"aclk_vio0",
	"aclk_vio1",
	"aclk_vop",
	"aclk_vopb",
	"aclk_vopl",
	"aclk_vo_pre",
	"aclk_vio_pre",
	"dclk_vop",
	"dclk_vop0",
	"dclk_vop1",
	"dclk_vopb",
	"dclk_vopl",
};

static struct clk **loader_clocks __initdata;
static int __init rockchip_clocks_loader_protect(void)
{
	int nclocks = ARRAY_SIZE(loader_protect_clocks);
	struct clk *clk;
	int i;

	loader_clocks = kcalloc(nclocks, sizeof(void *), GFP_KERNEL);
	if (!loader_clocks)
		return -ENOMEM;

	for (i = 0; i < nclocks; i++) {
		clk = __clk_lookup(loader_protect_clocks[i]);

		if (clk) {
			loader_clocks[i] = clk;
			clk_prepare_enable(clk);
		}
	}

	return 0;
}
arch_initcall_sync(rockchip_clocks_loader_protect);

static int __init rockchip_clocks_loader_unprotect(void)
{
	int i;

	if (!loader_clocks)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(loader_protect_clocks); i++) {
		struct clk *clk = loader_clocks[i];

		if (clk)
			clk_disable_unprepare(clk);
	}
	kfree(loader_clocks);

	return 0;
}
late_initcall_sync(rockchip_clocks_loader_unprotect);
#endif

int rockchip_drm_crtc_send_mcu_cmd(struct drm_device *drm_dev,
				   struct device_node *np_crtc,
				   u32 type, u32 value)
{
	struct drm_crtc *crtc;
	int pipe = 0;
	struct rockchip_drm_private *priv;

	if (!np_crtc || !of_device_is_available(np_crtc))
		return -EINVAL;

	drm_for_each_crtc(crtc, drm_dev) {
		if (of_get_parent(crtc->port) == np_crtc)
			break;
	}

	pipe = drm_crtc_index(crtc);
	if (pipe >= ROCKCHIP_MAX_CRTC)
		return -EINVAL;
	priv = crtc->dev->dev_private;
	if (priv->crtc_funcs[pipe]->crtc_send_mcu_cmd)
		priv->crtc_funcs[pipe]->crtc_send_mcu_cmd(crtc, type, value);

	return 0;
}
EXPORT_SYMBOL(rockchip_drm_crtc_send_mcu_cmd);

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

static int rockchip_drm_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;
	int ret;

	ret = drm_debugfs_create_files(rockchip_debugfs_files,
				       ARRAY_SIZE(rockchip_debugfs_files),
				       minor->debugfs_root,
				       minor);
	if (ret) {
		dev_err(dev->dev, "could not install rockchip_debugfs_list\n");
		return ret;
	}

	drm_for_each_crtc(crtc, dev) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->debugfs_init)
			priv->crtc_funcs[pipe]->debugfs_init(minor, crtc);
	}

	return 0;
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

	return drm_mode_create_tv_properties(dev, 0, NULL);
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
	drm_atomic_state_put(state);

err_unlock:
	drm_modeset_unlock_all(drm);
}

static bool is_support_hotplug(uint32_t output_type)
{
	switch (output_type) {
	case DRM_MODE_CONNECTOR_DVII:
	case DRM_MODE_CONNECTOR_DVID:
	case DRM_MODE_CONNECTOR_DVIA:
	case DRM_MODE_CONNECTOR_DisplayPort:
	case DRM_MODE_CONNECTOR_HDMIA:
	case DRM_MODE_CONNECTOR_HDMIB:
	case DRM_MODE_CONNECTOR_TV:
		return true;
	default:
		return false;
	}
}

static int rockchip_drm_bind(struct device *dev)
{
	struct drm_device *drm_dev;
	struct rockchip_drm_private *private;
	int ret;
	struct device_node *np = dev->of_node;
	struct device_node *parent_np;
	struct drm_crtc *crtc;

	drm_dev = drm_dev_alloc(&rockchip_drm_driver, dev);
	if (IS_ERR(drm_dev))
		return PTR_ERR(drm_dev);

	dev_set_drvdata(dev, drm_dev);

	private = devm_kzalloc(drm_dev->dev, sizeof(*private), GFP_KERNEL);
	if (!private) {
		ret = -ENOMEM;
		goto err_free;
	}

	mutex_init(&private->commit_lock);
	INIT_WORK(&private->commit_work, rockchip_drm_atomic_work);
	drm_dev->dev_private = private;

	private->dmc_support = false;
	private->devfreq = devfreq_get_devfreq_by_phandle(dev, 0);
	if (IS_ERR(private->devfreq)) {
		if (PTR_ERR(private->devfreq) == -EPROBE_DEFER) {
			parent_np = of_parse_phandle(np, "devfreq", 0);
			if (parent_np &&
			    of_device_is_available(parent_np)) {
				private->dmc_support = true;
				dev_warn(dev, "defer getting devfreq\n");
			} else {
				dev_info(dev, "dmc is disabled\n");
			}
		} else {
			dev_info(dev, "devfreq is not set\n");
		}
		private->devfreq = NULL;
	} else {
		private->dmc_support = true;
		dev_info(dev, "devfreq is ready\n");
	}
	private->hdmi_pll.pll = devm_clk_get(dev, "hdmi-tmds-pll");
	if (PTR_ERR(private->hdmi_pll.pll) == -ENOENT) {
		private->hdmi_pll.pll = NULL;
	} else if (PTR_ERR(private->hdmi_pll.pll) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto err_free;
	} else if (IS_ERR(private->hdmi_pll.pll)) {
		dev_err(dev, "failed to get hdmi-tmds-pll\n");
		ret = PTR_ERR(private->hdmi_pll.pll);
		goto err_free;
	}
	private->default_pll.pll = devm_clk_get(dev, "default-vop-pll");
	if (PTR_ERR(private->default_pll.pll) == -ENOENT) {
		private->default_pll.pll = NULL;
	} else if (PTR_ERR(private->default_pll.pll) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto err_free;
	} else if (IS_ERR(private->default_pll.pll)) {
		dev_err(dev, "failed to get default vop pll\n");
		ret = PTR_ERR(private->default_pll.pll);
		goto err_free;
	}

	INIT_LIST_HEAD(&private->psr_list);
	mutex_init(&private->psr_list_lock);

	ret = rockchip_drm_init_iommu(drm_dev);
	if (ret)
		goto err_free;

	drm_mode_config_init(drm_dev);

	rockchip_drm_mode_config_init(drm_dev);
	rockchip_drm_create_properties(drm_dev);
	/* Try to bind all sub drivers. */
	ret = component_bind_all(dev, drm_dev);
	if (ret)
		goto err_mode_config_cleanup;

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
#ifndef MODULE
	show_loader_logo(drm_dev);
#endif
	ret = of_reserved_mem_device_init(drm_dev->dev);
	if (ret)
		DRM_DEBUG_KMS("No reserved memory region assign to drm\n");

	ret = rockchip_drm_fbdev_init(drm_dev);
	if (ret)
		goto err_kms_helper_poll_fini;

	drm_for_each_crtc(crtc, drm_dev) {
		struct drm_fb_helper *helper = private->fbdev_helper;
		struct rockchip_crtc_state *s = NULL;

		if (!helper)
			break;

		s = to_rockchip_crtc_state(crtc->state);
		if (is_support_hotplug(s->output_type))
			drm_framebuffer_get(helper->fb);
	}

	drm_dev->mode_config.allow_fb_modifiers = true;

	ret = drm_dev_register(drm_dev, 0);
	if (ret)
		goto err_fbdev_fini;

	return 0;
err_fbdev_fini:
	rockchip_drm_fbdev_fini(drm_dev);
err_kms_helper_poll_fini:
	rockchip_gem_pool_destroy(drm_dev);
	drm_kms_helper_poll_fini(drm_dev);
err_unbind_all:
	component_unbind_all(dev, drm_dev);
err_mode_config_cleanup:
	drm_mode_config_cleanup(drm_dev);
	rockchip_iommu_cleanup(drm_dev);
err_free:
	drm_dev->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
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
	drm_mode_config_cleanup(drm_dev);
	rockchip_iommu_cleanup(drm_dev);

	drm_dev->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
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
	struct rockchip_drm_private *priv = dev->dev_private;

	if (!priv->logo)
		drm_fb_helper_restore_fbdev_mode_unlocked(priv->fbdev_helper);
}

static const struct drm_ioctl_desc rockchip_ioctls[] = {
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_CREATE, rockchip_gem_create_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_MAP_OFFSET,
			  rockchip_gem_map_offset_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_GET_PHYS, rockchip_gem_get_phys_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
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

static const struct dma_buf_ops rockchip_drm_gem_prime_dmabuf_ops = {
	.attach = drm_gem_map_attach,
	.detach = drm_gem_map_detach,
	.map_dma_buf = drm_gem_map_dma_buf,
	.unmap_dma_buf = drm_gem_unmap_dma_buf,
	.release = drm_gem_dmabuf_release,
	.map = drm_gem_dmabuf_kmap,
	.unmap = drm_gem_dmabuf_kunmap,
	.mmap = drm_gem_dmabuf_mmap,
	.vmap = drm_gem_dmabuf_vmap,
	.vunmap = drm_gem_dmabuf_vunmap,
	.begin_cpu_access = rockchip_drm_gem_dmabuf_begin_cpu_access,
	.end_cpu_access = rockchip_drm_gem_dmabuf_end_cpu_access,
};

#ifdef CONFIG_ARCH_ROCKCHIP
static void drm_gem_prime_dmabuf_release_callback(void *data)
{
	struct drm_prime_callback_data *cb_data = data;

	if (cb_data && cb_data->obj && cb_data->obj->import_attach) {
		struct dma_buf_attachment *attach = cb_data->obj->import_attach;
		struct sg_table *sgt = cb_data->sgt;

		if (sgt)
			dma_buf_unmap_attachment(attach, sgt,
						 DMA_BIDIRECTIONAL);
		dma_buf_detach(attach->dmabuf, attach);
		drm_gem_object_put_unlocked(cb_data->obj);
		kfree(cb_data);
	}
}
#endif

static struct drm_gem_object *rockchip_drm_gem_prime_import_dev(struct drm_device *dev,
								struct dma_buf *dma_buf,
								struct device *attach_dev)
{
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct drm_gem_object *obj;
#ifdef CONFIG_ARCH_ROCKCHIP
	struct drm_prime_callback_data *cb_data = NULL;
#endif
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

#ifdef CONFIG_ARCH_ROCKCHIP
	cb_data = dma_buf_get_release_callback_data(dma_buf,
					drm_gem_prime_dmabuf_release_callback);
	if (cb_data && cb_data->obj && cb_data->obj->dev == dev) {
		drm_gem_object_get(cb_data->obj);
		return cb_data->obj;
	}
#endif

	if (!dev->driver->gem_prime_import_sg_table)
		return ERR_PTR(-EINVAL);

	attach = dma_buf_attach(dma_buf, attach_dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

	get_dma_buf(dma_buf);

#ifdef CONFIG_ARCH_ROCKCHIP
	cb_data = kmalloc(sizeof(*cb_data), GFP_KERNEL);
	if (!cb_data) {
		ret = -ENOMEM;
		goto fail_detach;
	}
#endif

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

#ifdef CONFIG_ARCH_ROCKCHIP
	cb_data->obj = obj;
	cb_data->sgt = sgt;
	dma_buf_set_release_callback(dma_buf,
			drm_gem_prime_dmabuf_release_callback, cb_data);
	dma_buf_put(dma_buf);
	drm_gem_object_get(obj);
#endif

	return obj;

fail_unmap:
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
fail_detach:
#ifdef CONFIG_ARCH_ROCKCHIP
	kfree(cb_data);
#endif
	dma_buf_detach(dma_buf, attach);
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}

static struct drm_gem_object *rockchip_drm_gem_prime_import(struct drm_device *dev,
							    struct dma_buf *dma_buf)
{
	return rockchip_drm_gem_prime_import_dev(dev, dma_buf, dev->dev);
}

static struct dma_buf *rockchip_drm_gem_prime_export(struct drm_device *dev,
						     struct drm_gem_object *obj,
						     int flags)
{
	struct dma_buf_export_info exp_info = {
		.exp_name = KBUILD_MODNAME, /* white lie for debug */
		.owner = dev->driver->fops->owner,
		.ops = &rockchip_drm_gem_prime_dmabuf_ops,
		.size = obj->size,
		.flags = flags,
		.priv = obj,
	};

	if (dev->driver->gem_prime_res_obj)
		exp_info.resv = dev->driver->gem_prime_res_obj(obj);

	return drm_gem_dmabuf_export(dev, &exp_info);
}

static struct drm_driver rockchip_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM |
				  DRIVER_PRIME | DRIVER_ATOMIC |
				  DRIVER_RENDER,
	.postclose		= rockchip_drm_postclose,
	.lastclose		= rockchip_drm_lastclose,
	.open			= rockchip_drm_open,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.gem_free_object_unlocked = rockchip_gem_free_object,
	.dumb_create		= rockchip_gem_dumb_create,
	.dumb_map_offset	= rockchip_gem_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,
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
	.patchlevel	= DRIVER_PATCH,
};

#ifdef CONFIG_PM_SLEEP
static void rockchip_drm_fb_suspend(struct drm_device *drm)
{
	struct rockchip_drm_private *priv = drm->dev_private;

	console_lock();
	drm_fb_helper_set_suspend(priv->fbdev_helper, 1);
	console_unlock();
}

static void rockchip_drm_fb_resume(struct drm_device *drm)
{
	struct rockchip_drm_private *priv = drm->dev_private;

	console_lock();
	drm_fb_helper_set_suspend(priv->fbdev_helper, 0);
	console_unlock();
}

static int rockchip_drm_sys_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct rockchip_drm_private *priv;

	if (!drm)
		return 0;

	drm_kms_helper_poll_disable(drm);
	rockchip_drm_fb_suspend(drm);

	priv = drm->dev_private;
	priv->state = drm_atomic_helper_suspend(drm);
	if (IS_ERR(priv->state)) {
		rockchip_drm_fb_resume(drm);
		drm_kms_helper_poll_enable(drm);
		return PTR_ERR(priv->state);
	}

	return 0;
}

static int rockchip_drm_sys_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct rockchip_drm_private *priv;

	if (!drm)
		return 0;

	priv = drm->dev_private;
	drm_atomic_helper_resume(drm, priv->state);
	rockchip_drm_fb_resume(drm);
	drm_kms_helper_poll_enable(drm);

	return 0;
}
#endif

static const struct dev_pm_ops rockchip_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_drm_sys_suspend,
				rockchip_drm_sys_resume)
};

#define MAX_ROCKCHIP_SUB_DRIVERS 16
static struct platform_driver *rockchip_sub_drivers[MAX_ROCKCHIP_SUB_DRIVERS];
static int num_rockchip_sub_drivers;

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
			d = bus_find_device(&platform_bus_type, p, &drv->driver,
					    (void *)platform_bus_type.match);
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
		if (!iommu || !of_device_is_available(iommu->parent)) {
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
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_lvds_driver,
				CONFIG_ROCKCHIP_LVDS);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_dp_driver,
				CONFIG_ROCKCHIP_ANALOGIX_DP);
	ADD_ROCKCHIP_SUB_DRIVER(cdn_dp_driver, CONFIG_ROCKCHIP_CDN_DP);
	ADD_ROCKCHIP_SUB_DRIVER(dw_hdmi_rockchip_pltfm_driver,
				CONFIG_ROCKCHIP_DW_HDMI);
	ADD_ROCKCHIP_SUB_DRIVER(dw_mipi_dsi_driver,
				CONFIG_ROCKCHIP_DW_MIPI_DSI);
	ADD_ROCKCHIP_SUB_DRIVER(inno_hdmi_driver, CONFIG_ROCKCHIP_INNO_HDMI);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_tve_driver,
				CONFIG_ROCKCHIP_DRM_TVE);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_rgb_driver, CONFIG_ROCKCHIP_RGB);

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
