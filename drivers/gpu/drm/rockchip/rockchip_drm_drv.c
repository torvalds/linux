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
#include <drm/drm_sync_helper.h>
#include <drm/rockchip_drm.h>
#include <linux/devfreq.h>
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
#include <linux/fence.h>
#include <linux/console.h>
#include <linux/iommu.h>
#include <linux/of_reserved_mem.h>

#include <drm/rockchip_drm.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_fbdev.h"
#include "rockchip_drm_gem.h"

#define DRIVER_NAME	"rockchip"
#define DRIVER_DESC	"RockChip Soc DRM"
#define DRIVER_DATE	"20140818"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0
#define DRIVER_VERSION	"v1.0.1"

/***********************************************************************
 *  Rockchip DRM driver version
 *
 *  v1.0.0	: add basic version for rockchip drm driver(hjc)
 *  v1.0.1	: set frame start to field start for interlace mode(hjc)
 *
 **********************************************************************/

static bool is_support_iommu = true;

static LIST_HEAD(rockchip_drm_subdrv_list);
static DEFINE_MUTEX(subdrv_list_mutex);
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
	int crtc_hsync_end;
	int crtc_vsync_end;

	int left_margin;
	int right_margin;
	int top_margin;
	int bottom_margin;

	bool mode_changed;
	bool ymirror;
	int ratio;
};

#ifndef MODULE
static struct drm_crtc *find_crtc_by_node(struct drm_device *drm_dev,
					  struct device_node *node)
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
	struct drm_connector *connector;

	np_connector = of_graph_get_remote_port_parent(node);
	if (!np_connector || !of_device_is_available(np_connector))
		return NULL;

	drm_for_each_connector(connector, drm_dev) {
		if (connector->port == np_connector)
			return connector;
	}

	return NULL;
}

static
struct drm_connector *find_connector_by_bridge(struct drm_device *drm_dev,
					       struct device_node *node)
{
	struct device_node *np_encoder, *np_connector = NULL;
	struct drm_encoder *encoder;
	struct drm_connector *connector = NULL;
	struct device_node *port, *endpoint;
	bool encoder_bridge = false;
	bool found_connector = false;

	np_encoder = of_graph_get_remote_port_parent(node);
	if (!np_encoder || !of_device_is_available(np_encoder))
		goto err_put_encoder;
	drm_for_each_encoder(encoder, drm_dev) {
		if (encoder->port == np_encoder && encoder->bridge) {
			encoder_bridge = true;
			break;
		}
	}
	if (!encoder_bridge) {
		dev_err(drm_dev->dev, "can't found encoder bridge!\n");
		goto err_put_encoder;
	}
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

	drm_for_each_connector(connector, drm_dev) {
		if (connector->port == np_connector) {
			found_connector = true;
			break;
		}
	}

	if (!found_connector)
		connector = NULL;

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
	start = phys_to_virt(logo->start);
	end = phys_to_virt(logo->start + logo->size);

	if (private->domain) {
		iommu_unmap(private->domain, logo->dma_addr,
			    logo->iommu_map_size);
		drm_mm_remove_node(&logo->mm);
	} else {
		dma_unmap_sg(drm->dev, logo->sgt->sgl,
			     logo->sgt->nents, DMA_TO_DEVICE);
	}
	sg_free_table(logo->sgt);
	memblock_free(logo->start, logo->size);
	free_reserved_area(start, end, -1, "drm_logo");
	kfree(logo);
	private->logo = NULL;
}

static int init_loader_memory(struct drm_device *drm_dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct rockchip_logo *logo;
	struct device_node *np = drm_dev->dev->of_node;
	struct device_node *node;
	unsigned long nr_pages;
	struct page **pages;
	struct sg_table *sgt;
	phys_addr_t start, size;
	struct resource res;
	int i, ret;

	node = of_parse_phandle(np, "logo-memory-region", 0);
	if (!node)
		return -ENOMEM;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return ret;
	start = res.start;
	size = resource_size(&res);
	if (!size)
		return -ENOMEM;

	logo = kmalloc(sizeof(*logo), GFP_KERNEL);
	if (!logo)
		return -ENOMEM;

	logo->kvaddr = phys_to_virt(start);
	nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	pages = kmalloc_array(nr_pages, sizeof(*pages),	GFP_KERNEL);
	if (!pages)
		goto err_free_logo;
	i = 0;
	while (i < nr_pages) {
		pages[i] = phys_to_page(start);
		start += PAGE_SIZE;
		i++;
	}
	sgt = drm_prime_pages_to_sg(pages, nr_pages);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto err_free_pages;
	}

	if (private->domain) {
		memset(&logo->mm, 0, sizeof(logo->mm));
		ret = drm_mm_insert_node_generic(&private->mm, &logo->mm,
						 size, PAGE_SIZE,
						 0, 0, 0);
		if (ret < 0) {
			DRM_ERROR("out of I/O virtual memory: %d\n", ret);
			goto err_free_pages;
		}

		logo->dma_addr = logo->mm.start;

		logo->iommu_map_size = iommu_map_sg(private->domain,
						    logo->dma_addr, sgt->sgl,
						    sgt->nents, IOMMU_READ);
		if (logo->iommu_map_size < size) {
			DRM_ERROR("failed to map buffer");
			ret = -ENOMEM;
			goto err_remove_node;
		}
	} else {
		dma_map_sg(drm_dev->dev, sgt->sgl, sgt->nents, DMA_TO_DEVICE);
		logo->dma_addr = sg_dma_address(sgt->sgl);
	}

	logo->sgt = sgt;
	logo->start = res.start;
	logo->size = size;
	logo->count = 1;
	private->logo = logo;

	return 0;

err_remove_node:
	drm_mm_remove_node(&logo->mm);
err_free_pages:
	kfree(pages);
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
		mode_cmd.pixel_format = DRM_FORMAT_BGR565;
		break;
	case 24:
		mode_cmd.pixel_format = DRM_FORMAT_BGR888;
		break;
	case 32:
		mode_cmd.pixel_format = DRM_FORMAT_XRGB8888;
		break;
	default:
		pr_err("%s: unsupport to logo bpp %d\n", __func__, bpp);
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
		drm_framebuffer_unreference(fb);
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

	if (!of_property_read_u32(route, "logo,ymirror", &val))
		set->ymirror = val;

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

	if (!set->hdisplay || !set->vdisplay || !set->vrefresh)
		is_crtc_enabled = false;

	conn_state = drm_atomic_get_connector_state(state, connector);
	if (IS_ERR(conn_state))
		return PTR_ERR(conn_state);

	funcs = connector->helper_private;
	conn_state->best_encoder = funcs->best_encoder(connector);
	if (funcs->loader_protect)
		funcs->loader_protect(connector, true);
	connector->loader_protect = true;
	encoder_funcs = conn_state->best_encoder->helper_private;
	if (encoder_funcs->loader_protect)
		encoder_funcs->loader_protect(conn_state->best_encoder, true);
	conn_state->best_encoder->loader_protect = true;
	num_modes = connector->funcs->fill_modes(connector, 4096, 4096);
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
		    mode->flags == set->flags) {
			found = 1;
			match = 1;
			break;
		}
	}

	if (!found) {
		ret = -EINVAL;
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
	struct rockchip_drm_private *priv = drm_dev->dev_private;
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
		if (!connector_helper_funcs ||
		    !connector_helper_funcs->best_encoder)
			return -ENXIO;
		encoder = connector_helper_funcs->best_encoder(connector);
		if (!encoder)
			return -ENXIO;
		encoder_helper_funcs = encoder->helper_private;
		if (!encoder_helper_funcs->atomic_check)
			return -ENXIO;
		ret = encoder_helper_funcs->atomic_check(encoder, crtc->state,
							 conn_state);
		if (ret)
			return ret;
		if (encoder_helper_funcs->mode_set)
			encoder_helper_funcs->mode_set(encoder, mode, mode);
	}

	primary_state = drm_atomic_get_plane_state(state, crtc->primary);
	if (IS_ERR(primary_state))
		return PTR_ERR(primary_state);

	crtc_state->plane_mask = 1 << drm_plane_index(crtc->primary);
	*plane_mask |= crtc_state->plane_mask;

	drm_atomic_set_fb_for_plane(primary_state, set->fb);
	drm_framebuffer_unreference(set->fb);
	ret = drm_atomic_set_crtc_for_plane(primary_state, crtc);

	if (set->ymirror)
		/*
		 * TODO:
		 * some vop maybe not support ymirror, but force use it now.
		 */
		drm_atomic_plane_set_property(crtc->primary, primary_state,
					      priv->logo_ymirror_prop,
					      true);

	return ret;
}

static bool is_support_hotplug(uint32_t output_type)
{
	switch (output_type) {
	case DRM_MODE_CONNECTOR_Unknown:
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

static void show_loader_logo(struct drm_device *drm_dev)
{
	struct drm_atomic_state *state, *old_state;
	struct device_node *np = drm_dev->dev->of_node;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct device_node *root, *route;
	struct rockchip_drm_mode_set *set, *tmp, *unset;
	struct list_head mode_set_list;
	struct list_head mode_unset_list;
	unsigned plane_mask = 0;
	int ret;

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
			drm_framebuffer_unreference(set->fb);
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
		int found_used_crtc = 0;

		list_for_each_entry_safe(set, tmp_set, &mode_set_list, head) {
			if (set->crtc == unset->crtc) {
				found_used_crtc = 1;
				continue;
			}
		}
		if (!found_used_crtc) {
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
	 * drm deivces as old state, so if new state come, can compare
	 * with this state to judge which status need to update.
	 */
	drm_atomic_helper_swap_state(drm_dev, state);
	drm_atomic_state_free(state);
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

	ret = drm_atomic_commit(state);
	drm_atomic_clean_old_fb(drm_dev, plane_mask, ret);

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
		drm_atomic_helper_swap_state(drm_dev, old_state);
		goto err_free_old_state;
	}

	rockchip_free_loader_memory(drm_dev);
	drm_atomic_state_free(old_state);

	drm_modeset_unlock_all(drm_dev);
	return;

err_free_old_state:
	drm_atomic_state_free(old_state);
err_free_state:
	drm_atomic_state_free(state);
err_unlock:
	drm_modeset_unlock_all(drm_dev);
	if (ret)
		dev_err(drm_dev->dev, "failed to show loader logo\n");
	rockchip_free_loader_memory(drm_dev);
}

static const char *const loader_protect_clocks[] __initconst = {
	"hclk_vio",
	"aclk_vio",
	"aclk_vio0",
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
fs_initcall(rockchip_clocks_loader_protect);

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
		dev_err(dev, "Failed to attach iommu device\n");
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

static struct drm_crtc *rockchip_crtc_from_pipe(struct drm_device *drm,
						int pipe)
{
	struct drm_crtc *crtc;
	int i = 0;

	list_for_each_entry(crtc, &drm->mode_config.crtc_list, head)
		if (i++ == pipe)
			return crtc;

	return NULL;
}

static int rockchip_drm_crtc_enable_vblank(struct drm_device *dev,
					   unsigned int pipe)
{
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc = rockchip_crtc_from_pipe(dev, pipe);

	if (crtc && priv->crtc_funcs[pipe] &&
	    priv->crtc_funcs[pipe]->enable_vblank)
		return priv->crtc_funcs[pipe]->enable_vblank(crtc);

	return 0;
}

static void rockchip_drm_crtc_disable_vblank(struct drm_device *dev,
					     unsigned int pipe)
{
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc = rockchip_crtc_from_pipe(dev, pipe);

	if (crtc && priv->crtc_funcs[pipe] &&
	    priv->crtc_funcs[pipe]->disable_vblank)
		priv->crtc_funcs[pipe]->disable_vblank(crtc);
}

static int rockchip_drm_fault_handler(struct iommu_domain *iommu,
				      struct device *dev,
				      unsigned long iova, int flags, void *arg)
{
	struct drm_device *drm_dev = arg;
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_crtc *crtc;

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
	int ret;

	if (!priv->domain)
		return 0;

	mutex_lock(&priv->mm_lock);

	ret = drm_mm_dump_table(s, &priv->mm);

	mutex_unlock(&priv->mm_lock);

	return ret;
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

static void rockchip_drm_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(rockchip_debugfs_files,
				 ARRAY_SIZE(rockchip_debugfs_files), minor);
}
#endif

static int rockchip_drm_create_properties(struct drm_device *dev)
{
	struct drm_property *prop;
	struct rockchip_drm_private *private = dev->dev_private;
	const struct drm_prop_enum_list cabc_mode_enum_list[] = {
		{ ROCKCHIP_DRM_CABC_MODE_DISABLE, "Disable" },
		{ ROCKCHIP_DRM_CABC_MODE_NORMAL, "Normal" },
		{ ROCKCHIP_DRM_CABC_MODE_LOWPOWER, "LowPower" },
		{ ROCKCHIP_DRM_CABC_MODE_USERSPACE, "Userspace" },
	};

	prop = drm_property_create_enum(dev, 0, "CABC_MODE", cabc_mode_enum_list,
			ARRAY_SIZE(cabc_mode_enum_list));

	private->cabc_mode_property = prop;

	prop = drm_property_create(dev, DRM_MODE_PROP_BLOB, "CABC_LUT", 0);
	if (!prop)
		return -ENOMEM;
	private->cabc_lut_property = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					"CABC_STAGE_UP", 0, 512);
	if (!prop)
		return -ENOMEM;
	private->cabc_stage_up_property = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					"CABC_STAGE_DOWN", 0, 255);
	if (!prop)
		return -ENOMEM;
	private->cabc_stage_down_property = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					"CABC_GLOBAL_DN", 0, 255);
	if (!prop)
		return -ENOMEM;
	private->cabc_global_dn_property = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					"CABC_CALC_PIXEL_NUM", 0, 1000);
	if (!prop)
		return -ENOMEM;
	private->cabc_calc_pixel_num_property = prop;
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

	mutex_lock(&drm->mode_config.mutex);

	drm_for_each_connector(connector, drm) {
#define ROCKCHIP_PROP_ATTACH(prop, v) \
	drm_object_attach_property(&connector->base, prop, v)

		ROCKCHIP_PROP_ATTACH(conf->tv_brightness_property, 100);
		ROCKCHIP_PROP_ATTACH(conf->tv_contrast_property, 100);
		ROCKCHIP_PROP_ATTACH(conf->tv_saturation_property, 100);
		ROCKCHIP_PROP_ATTACH(conf->tv_hue_property, 100);
	}

	mutex_unlock(&drm->mode_config.mutex);
}

static void rockchip_drm_set_property_default(struct drm_device *drm)
{
	struct drm_connector *connector;
	struct drm_mode_config *conf = &drm->mode_config;
	struct drm_atomic_state *state;
	int ret;

	drm_modeset_lock_all(drm);

	state = drm_atomic_helper_duplicate_state(drm, conf->acquire_ctx);
	if (!state) {
		DRM_ERROR("failed to alloc atomic state\n");
		goto err_unlock;
	}
	state->acquire_ctx = conf->acquire_ctx;

	drm_for_each_connector(connector, drm) {
		struct drm_connector_state *connector_state;

		connector_state = drm_atomic_get_connector_state(state,
								 connector);
		if (IS_ERR(connector_state))
			DRM_ERROR("Connector[%d]: Failed to get state\n",
				  connector->base.id);

#define CONNECTOR_SET_PROP(prop, val) \
	do { \
		ret = drm_atomic_connector_set_property(connector, \
						connector_state, \
						prop, \
						val); \
		if (ret) \
			DRM_ERROR("Connector[%d]: Failed to initial %s\n", \
				  connector->base.id, #prop); \
	} while (0)

		CONNECTOR_SET_PROP(conf->tv_brightness_property, 50);
		CONNECTOR_SET_PROP(conf->tv_contrast_property, 50);
		CONNECTOR_SET_PROP(conf->tv_saturation_property, 50);
		CONNECTOR_SET_PROP(conf->tv_hue_property, 50);

#undef CONNECTOR_SET_PROP
	}

	ret = drm_atomic_commit(state);
	WARN_ON(ret == -EDEADLK);
	if (ret)
		DRM_ERROR("Failed to update propertys\n");

err_unlock:
	drm_modeset_unlock_all(drm);
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
	if (!drm_dev)
		return -ENOMEM;

	ret = drm_dev_set_unique(drm_dev, "%s", dev_name(dev));
	if (ret)
		goto err_free;

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

#ifdef CONFIG_DRM_DMA_SYNC
	private->cpu_fence_context = fence_context_alloc(1);
	atomic_set(&private->cpu_fence_seqno, 0);
#endif

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

	/*
	 * with vblank_disable_allowed = true, vblank interrupt will be disabled
	 * by drm timer once a current process gives up ownership of
	 * vblank event.(after drm_vblank_put function is called)
	 */
	drm_dev->vblank_disable_allowed = true;

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

		s = to_rockchip_crtc_state(crtc->state);
		if (is_support_hotplug(s->output_type)) {
			s->crtc_primary_fb = crtc->primary->fb;
			crtc->primary->fb = helper->fb;
			drm_framebuffer_reference(helper->fb);
		}
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
	drm_vblank_cleanup(drm_dev);
err_unbind_all:
	component_unbind_all(dev, drm_dev);
err_mode_config_cleanup:
	drm_mode_config_cleanup(drm_dev);
	rockchip_iommu_cleanup(drm_dev);
err_free:
	drm_dev->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
	drm_dev_unref(drm_dev);
	return ret;
}

static void rockchip_drm_unbind(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	drm_dev_unregister(drm_dev);

	rockchip_drm_fbdev_fini(drm_dev);
	rockchip_gem_pool_destroy(drm_dev);
	drm_kms_helper_poll_fini(drm_dev);

	drm_vblank_cleanup(drm_dev);
	component_unbind_all(dev, drm_dev);
	drm_mode_config_cleanup(drm_dev);
	rockchip_iommu_cleanup(drm_dev);

	drm_dev->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
	drm_dev_unref(drm_dev);
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

int rockchip_drm_register_subdrv(struct drm_rockchip_subdrv *subdrv)
{
	if (!subdrv)
		return -EINVAL;

	mutex_lock(&subdrv_list_mutex);
	list_add_tail(&subdrv->list, &rockchip_drm_subdrv_list);
	mutex_unlock(&subdrv_list_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_drm_register_subdrv);

int rockchip_drm_unregister_subdrv(struct drm_rockchip_subdrv *subdrv)
{
	if (!subdrv)
		return -EINVAL;

	mutex_lock(&subdrv_list_mutex);
	list_del(&subdrv->list);
	mutex_unlock(&subdrv_list_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_drm_unregister_subdrv);

static int rockchip_drm_open(struct drm_device *dev, struct drm_file *file)
{
	struct rockchip_drm_file_private *file_priv;
	struct drm_rockchip_subdrv *subdrv;
	struct drm_crtc *crtc;
	int ret = 0;

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;
	INIT_LIST_HEAD(&file_priv->gem_cpu_acquire_list);

	file->driver_priv = file_priv;

	mutex_lock(&subdrv_list_mutex);
	list_for_each_entry(subdrv, &rockchip_drm_subdrv_list, list) {
		ret = subdrv->open(dev, subdrv->dev, file);
		if (ret) {
			mutex_unlock(&subdrv_list_mutex);
			goto err_free_file_priv;
		}
	}
	mutex_unlock(&subdrv_list_mutex);

	drm_for_each_crtc(crtc, dev) {
		struct rockchip_crtc_state *s = NULL;

		s = to_rockchip_crtc_state(crtc->state);
		if (s->crtc_primary_fb) {
			crtc->primary->fb = s->crtc_primary_fb;
			s->crtc_primary_fb = NULL;
		}
	}

	return 0;

err_free_file_priv:
	kfree(file_priv);
	file_priv = NULL;

	return ret;
}

static void rockchip_drm_preclose(struct drm_device *dev,
				  struct drm_file *file_priv)
{
	struct rockchip_drm_file_private *file_private = file_priv->driver_priv;
	struct rockchip_gem_object_node *cur, *d;
	struct drm_rockchip_subdrv *subdrv;
	struct drm_crtc *crtc;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		rockchip_drm_crtc_cancel_pending_vblank(crtc, file_priv);

	mutex_lock(&dev->struct_mutex);
	list_for_each_entry_safe(cur, d,
			&file_private->gem_cpu_acquire_list, list) {
#ifdef CONFIG_DRM_DMA_SYNC
		BUG_ON(!cur->rockchip_gem_obj->acquire_fence);
		drm_fence_signal_and_put(&cur->rockchip_gem_obj->acquire_fence);
#endif
		drm_gem_object_unreference(&cur->rockchip_gem_obj->base);
		kfree(cur);
	}
	/* since we are deleting the whole list, just initialize the header
	 * instead of calling list_del for every element
	 */
	INIT_LIST_HEAD(&file_private->gem_cpu_acquire_list);
	mutex_unlock(&dev->struct_mutex);

	mutex_lock(&subdrv_list_mutex);
	list_for_each_entry(subdrv, &rockchip_drm_subdrv_list, list)
		subdrv->close(dev, subdrv->dev, file_priv);
	mutex_unlock(&subdrv_list_mutex);
}

static void rockchip_drm_postclose(struct drm_device *dev, struct drm_file *file)
{
	kfree(file->driver_priv);
	file->driver_priv = NULL;
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
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_CPU_ACQUIRE,
			  rockchip_gem_cpu_acquire_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_CPU_RELEASE,
			  rockchip_gem_cpu_release_ioctl,
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
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.release = drm_release,
};

const struct vm_operations_struct rockchip_drm_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static struct drm_driver rockchip_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM |
				  DRIVER_PRIME | DRIVER_ATOMIC |
				  DRIVER_RENDER,
	.preclose		= rockchip_drm_preclose,
	.lastclose		= rockchip_drm_lastclose,
	.get_vblank_counter	= drm_vblank_no_hw_counter,
	.open			= rockchip_drm_open,
	.postclose		= rockchip_drm_postclose,
	.enable_vblank		= rockchip_drm_crtc_enable_vblank,
	.disable_vblank		= rockchip_drm_crtc_disable_vblank,
	.gem_vm_ops		= &rockchip_drm_vm_ops,
	.gem_free_object	= rockchip_gem_free_object,
	.dumb_create		= rockchip_gem_dumb_create,
	.dumb_map_offset	= rockchip_gem_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_get_sg_table	= rockchip_gem_prime_get_sg_table,
	.gem_prime_import_sg_table	= rockchip_gem_prime_import_sg_table,
	.gem_prime_vmap		= rockchip_gem_prime_vmap,
	.gem_prime_vunmap	= rockchip_gem_prime_vunmap,
	.gem_prime_mmap		= rockchip_gem_mmap_buf,
	.gem_prime_begin_cpu_access = rockchip_gem_prime_begin_cpu_access,
	.gem_prime_end_cpu_access = rockchip_gem_prime_end_cpu_access,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init		= rockchip_drm_debugfs_init,
	.debugfs_cleanup	= rockchip_drm_debugfs_cleanup,
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
	struct drm_device *drm;
	struct rockchip_drm_private *priv;

	drm = dev_get_drvdata(dev);
	if (!drm) {
		DRM_ERROR("%s: Failed to get drm device!\n", __func__);
		return 0;
	}

	priv = drm->dev_private;
	drm_kms_helper_poll_disable(drm);
	rockchip_drm_fb_suspend(drm);

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
	struct rockchip_drm_private *priv = drm->dev_private;

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

static int compare_of(struct device *dev, void *data)
{
	struct device_node *np = data;

	return dev->of_node == np;
}

static void rockchip_add_endpoints(struct device *dev,
				   struct component_match **match,
				   struct device_node *port)
{
	struct device_node *ep, *remote;

	for_each_child_of_node(port, ep) {
		remote = of_graph_get_remote_port_parent(ep);
		if (!remote || !of_device_is_available(remote)) {
			of_node_put(remote);
			continue;
		} else if (!of_device_is_available(remote->parent)) {
			dev_warn(dev, "parent device of %s is not available\n",
				 remote->full_name);
			of_node_put(remote);
			continue;
		}

		component_match_add(dev, match, compare_of, remote);
		of_node_put(remote);
	}
}

static const struct component_master_ops rockchip_drm_ops = {
	.bind = rockchip_drm_bind,
	.unbind = rockchip_drm_unbind,
};

static int rockchip_drm_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct component_match *match = NULL;
	struct device_node *np = dev->of_node;
	struct device_node *port;
	int i;

	DRM_INFO("Rockchip DRM driver version: %s\n", DRIVER_VERSION);
	if (!np)
		return -ENODEV;
	/*
	 * Bind the crtc ports first, so that
	 * drm_of_find_possible_crtcs called from encoder .bind callbacks
	 * works as expected.
	 */
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
			dev_dbg(dev, "no iommu attached for %s, using non-iommu buffers\n",
				port->parent->full_name);
			/*
			 * if there is a crtc not support iommu, force set all
			 * crtc use non-iommu buffer.
			 */
			is_support_iommu = false;
		}

		component_match_add(dev, &match, compare_of, port->parent);
		of_node_put(port);
	}

	if (i == 0) {
		dev_err(dev, "missing 'ports' property\n");
		return -ENODEV;
	}

	if (!match) {
		dev_err(dev, "No available vop found for display-subsystem.\n");
		return -ENODEV;
	}
	/*
	 * For each bound crtc, bind the encoders attached to its
	 * remote endpoint.
	 */
	for (i = 0;; i++) {
		port = of_parse_phandle(np, "ports", i);
		if (!port)
			break;

		if (!of_device_is_available(port->parent)) {
			of_node_put(port);
			continue;
		}

		rockchip_add_endpoints(dev, &match, port);
		of_node_put(port);
	}

	port = of_parse_phandle(np, "backlight", 0);
	if (port && of_device_is_available(port)) {
		component_match_add(dev, &match, compare_of, port);
		of_node_put(port);
	}

	return component_master_add_with_match(dev, &rockchip_drm_ops, match);
}

static int rockchip_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &rockchip_drm_ops);

	return 0;
}

static void rockchip_drm_platform_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct drm_crtc *crtc;
	struct drm_device *drm;
	struct rockchip_drm_private *priv;

	drm = dev_get_drvdata(dev);
	if (!drm) {
		DRM_ERROR("%s: Failed to get drm device!\n", __func__);
		return;
	}

	priv = drm->dev_private;
	drm_for_each_crtc(crtc, drm) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->crtc_close)
			priv->crtc_funcs[pipe]->crtc_close(crtc);
	}
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

module_platform_driver(rockchip_drm_platform_driver);

MODULE_AUTHOR("Mark Yao <mark.yao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP DRM Driver");
MODULE_LICENSE("GPL v2");
