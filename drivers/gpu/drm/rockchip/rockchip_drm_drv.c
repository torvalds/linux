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

#include <linux/dma-iommu.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_sync_helper.h>
#include <drm/rockchip_drm.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/component.h>
#include <linux/fence.h>
#include <linux/console.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_fbdev.h"
#include "rockchip_drm_gem.h"
#include "rockchip_drm_rga.h"

#define DRIVER_NAME	"rockchip"
#define DRIVER_DESC	"RockChip Soc DRM"
#define DRIVER_DATE	"20140818"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static LIST_HEAD(rockchip_drm_subdrv_list);
static DEFINE_MUTEX(subdrv_list_mutex);

struct rockchip_drm_mode_set {
	struct list_head head;
	struct drm_framebuffer *fb;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	struct drm_display_mode *mode;
	int hdisplay;
	int vdisplay;
	int vrefresh;

	bool mode_changed;
	bool ymirror;
	int ratio;
};

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

static int init_loader_memory(struct drm_device *drm_dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct rockchip_logo *logo;
	struct device_node *np = drm_dev->dev->of_node;
	struct device_node *node;
	unsigned long nr_pages;
	struct page **pages;
	struct sg_table *sgt;
	DEFINE_DMA_ATTRS(attrs);
	phys_addr_t start, size;
	struct resource res;
	int i, ret;

	logo = devm_kmalloc(drm_dev->dev, sizeof(*logo), GFP_KERNEL);
	if (!logo)
		return -ENOMEM;

	node = of_parse_phandle(np, "memory-region", 0);
	if (!node)
		return -ENOMEM;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return ret;
	start = res.start;
	size = resource_size(&res);
	if (!size)
		return -ENOMEM;

	nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	pages = kmalloc_array(nr_pages, sizeof(*pages),	GFP_KERNEL);
	if (!pages)
		return -ENOMEM;
	i = 0;
	while (i < nr_pages) {
		pages[i] = phys_to_page(start);
		start += PAGE_SIZE;
		i++;
	}
	sgt = drm_prime_pages_to_sg(pages, nr_pages);
	if (IS_ERR(sgt)) {
		kfree(pages);
		return PTR_ERR(sgt);
	}

	dma_set_attr(DMA_ATTR_SKIP_CPU_SYNC, &attrs);
	dma_set_attr(DMA_ATTR_NO_KERNEL_MAPPING, &attrs);
	dma_map_sg_attrs(drm_dev->dev, sgt->sgl, sgt->nents,
			 DMA_TO_DEVICE, &attrs);
	logo->dma_addr = sg_dma_address(sgt->sgl);
	logo->sgt = sgt;
	logo->start = res.start;
	logo->size = size;
	logo->count = 0;
	private->logo = logo;

	return 0;
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

	mode_cmd.pitches[0] = mode_cmd.width * bpp / 8;

	switch (bpp) {
	case 16:
		mode_cmd.pixel_format = DRM_FORMAT_BGR565;
		break;
	case 24:
		mode_cmd.pixel_format = DRM_FORMAT_BGR888;
		break;
	case 32:
		mode_cmd.pixel_format = DRM_FORMAT_XBGR8888;
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
	u32 val;

	connect = of_parse_phandle(route, "connect", 0);
	if (!connect)
		return NULL;

	fb = get_framebuffer_by_node(drm_dev, route);
	if (IS_ERR_OR_NULL(fb))
		return NULL;

	crtc = find_crtc_by_node(drm_dev, connect);
	connector = find_connector_by_node(drm_dev, connect);
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

	if (!of_property_read_u32(route, "video,vrefresh", &val))
		set->vrefresh = val;

	if (!of_property_read_u32(route, "logo,ymirror", &val))
		set->ymirror = val;

	set->fb = fb;
	set->crtc = crtc;
	set->connector = connector;
	/* TODO: set display fullscreen or center */
	set->ratio = 0;

	return set;
}

int setup_initial_state(struct drm_device *drm_dev,
			struct drm_atomic_state *state,
			struct rockchip_drm_mode_set *set)
{
	struct drm_connector *connector = set->connector;
	struct drm_crtc *crtc = set->crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	struct drm_plane_state *primary_state;
	struct drm_display_mode *mode = NULL;
	const struct drm_connector_helper_funcs *funcs;
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
	num_modes = connector->funcs->fill_modes(connector, 4096, 4096);
	if (!num_modes) {
		dev_err(drm_dev->dev, "connector[%s] can't found any modes\n",
			connector->name);
		return -EINVAL;
	}

	list_for_each_entry(mode, &connector->modes, head) {
		if (mode->hdisplay == set->hdisplay &&
		    mode->vdisplay == set->vdisplay &&
		    drm_mode_vrefresh(mode) == set->vrefresh) {
			found = 1;
			match = 1;
			break;
		}
	}

	if (!found) {
		list_for_each_entry(mode, &connector->modes, head) {
			if (mode->type & DRM_MODE_TYPE_PREFERRED) {
				found = 1;
				break;
			}
		}

		if (!found) {
			mode = list_first_entry_or_null(&connector->modes,
							struct drm_display_mode,
							head);
			if (!mode) {
				pr_err("failed to find available modes\n");
				return -EINVAL;
			}
		}
	}

	set->mode = mode;
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	drm_mode_copy(&crtc_state->adjusted_mode, mode);
	if (!match || !is_crtc_enabled) {
		set->mode_changed = true;
	} else {
		ret = drm_atomic_set_crtc_for_connector(conn_state, crtc);
		if (ret)
			return ret;

		ret = drm_atomic_set_mode_for_crtc(crtc_state, mode);
		if (ret)
			return ret;

		crtc_state->active = true;
	}

	if (!set->fb)
		return 0;
	primary_state = drm_atomic_get_plane_state(state, crtc->primary);
	if (IS_ERR(primary_state))
		return PTR_ERR(primary_state);

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
}

static int update_state(struct drm_device *drm_dev,
			struct drm_atomic_state *state,
			struct rockchip_drm_mode_set *set,
			unsigned int *plane_mask)
{
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct drm_crtc *crtc = set->crtc;
	struct drm_connector *connector = set->connector;
	struct drm_display_mode *mode = set->mode;
	struct drm_plane_state *primary_state;
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	int ret;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);
	conn_state = drm_atomic_get_connector_state(state, connector);
	if (IS_ERR(conn_state))
		return PTR_ERR(conn_state);

	if (set->mode_changed) {
		ret = drm_atomic_set_crtc_for_connector(conn_state, crtc);
		if (ret)
			return ret;

		ret = drm_atomic_set_mode_for_crtc(crtc_state, mode);
		if (ret)
			return ret;

		crtc_state->active = true;
	} else {
		const struct drm_crtc_helper_funcs *funcs;

		funcs = crtc->helper_private;
		if (!funcs || !funcs->enable)
			return -ENXIO;
		funcs->enable(crtc);
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
					      mode_config->rotation_property,
					      BIT(DRM_REFLECT_Y));

	return ret;
}

static void show_loader_logo(struct drm_device *drm_dev)
{
	struct drm_atomic_state *state;
	struct device_node *np = drm_dev->dev->of_node;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct device_node *root, *route;
	struct rockchip_drm_mode_set *set, *tmp;
	struct list_head mode_set_list;
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
	drm_modeset_lock_all(drm_dev);
	state = drm_atomic_state_alloc(drm_dev);
	if (!state) {
		dev_err(drm_dev->dev, "failed to alloc atomic state\n");
		ret = -ENOMEM;
		goto err_unlock;
	}

	state->acquire_ctx = mode_config->acquire_ctx;

	for_each_child_of_node(root, route) {
		set = of_parse_display_resource(drm_dev, route);
		if (!set)
			continue;

		if (setup_initial_state(drm_dev, state, set)) {
			drm_framebuffer_unreference(set->fb);
			kfree(set);
			continue;
		}
		INIT_LIST_HEAD(&set->head);
		list_add_tail(&set->head, &mode_set_list);
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
	state = drm_atomic_helper_duplicate_state(drm_dev,
						  mode_config->acquire_ctx);
	if (IS_ERR(state)) {
		dev_err(drm_dev->dev, "failed to duplicate atomic state\n");
		ret = PTR_ERR_OR_ZERO(state);
		goto err_unlock;
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
		struct drm_crtc *crtc = set->crtc;

		list_del(&set->head);
		kfree(set);

		/* FIXME:
		 * primary plane state rotation is not BIT(0), but we only want
		 * it effect on logo display, userspace may not known to clean
		 * this property, would get unexpect display, so force set
		 * primary rotation to BIT(0).
		 */
		if (!crtc->primary || !crtc->primary->state)
			continue;

		drm_atomic_plane_set_property(crtc->primary,
					      crtc->primary->state,
					      mode_config->rotation_property,
					      BIT(0));
	}

	/*
	 * Is possible get deadlock here?
	 */
	WARN_ON(ret == -EDEADLK);

	if (ret)
		goto err_free_state;

	drm_modeset_unlock_all(drm_dev);
	return;

err_free_state:
	drm_atomic_state_free(state);
err_unlock:
	drm_modeset_unlock_all(drm_dev);
	if (ret)
		dev_err(drm_dev->dev, "failed to show loader logo\n");
}

/*
 * Attach a (component) device to the shared drm dma mapping from master drm
 * device.  This is used by the VOPs to map GEM buffers to a common DMA
 * mapping.
 */
int rockchip_drm_dma_attach_device(struct drm_device *drm_dev,
				   struct device *dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct iommu_domain *domain = private->domain;
	int ret;

	ret = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	dma_set_max_seg_size(dev, DMA_BIT_MASK(32));
	ret = iommu_attach_device(domain, dev);
	if (ret) {
		dev_err(dev, "Failed to attach iommu device\n");
		return ret;
	}

	if (!common_iommu_setup_dma_ops(dev, 0x10000000, SZ_2G, domain->ops)) {
		dev_err(dev, "Failed to set dma_ops\n");
		iommu_detach_device(domain, dev);
		ret = -ENODEV;
	}

	return ret;
}

void rockchip_drm_dma_detach_device(struct drm_device *drm_dev,
				    struct device *dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct iommu_domain *domain = private->domain;

	iommu_detach_device(domain, dev);
}

int rockchip_register_crtc_funcs(struct drm_crtc *crtc,
				 const struct rockchip_crtc_funcs *crtc_funcs)
{
	int pipe = drm_crtc_index(crtc);
	struct rockchip_drm_private *priv = crtc->dev->dev_private;

	if (pipe > ROCKCHIP_MAX_CRTC)
		return -EINVAL;

	priv->crtc_funcs[pipe] = crtc_funcs;

	return 0;
}

void rockchip_unregister_crtc_funcs(struct drm_crtc *crtc)
{
	int pipe = drm_crtc_index(crtc);
	struct rockchip_drm_private *priv = crtc->dev->dev_private;

	if (pipe > ROCKCHIP_MAX_CRTC)
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
	    priv->crtc_funcs[pipe]->enable_vblank)
		priv->crtc_funcs[pipe]->disable_vblank(crtc);
}

static int rockchip_drm_load(struct drm_device *drm_dev, unsigned long flags)
{
	struct rockchip_drm_private *private;
	struct device *dev = drm_dev->dev;
	struct drm_connector *connector;
	struct iommu_group *group;
	int ret;

	private = devm_kzalloc(drm_dev->dev, sizeof(*private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;

	mutex_init(&private->commit.lock);
	INIT_WORK(&private->commit.work, rockchip_drm_atomic_work);

	drm_dev->dev_private = private;

#ifdef CONFIG_DRM_DMA_SYNC
	private->cpu_fence_context = fence_context_alloc(1);
	atomic_set(&private->cpu_fence_seqno, 0);
#endif

	drm_mode_config_init(drm_dev);

	rockchip_drm_mode_config_init(drm_dev);

	dev->dma_parms = devm_kzalloc(dev, sizeof(*dev->dma_parms),
				      GFP_KERNEL);
	if (!dev->dma_parms) {
		ret = -ENOMEM;
		goto err_config_cleanup;
	}

	private->domain = iommu_domain_alloc(&platform_bus_type);
	if (!private->domain)
		return -ENOMEM;

	ret = iommu_get_dma_cookie(private->domain);
	if (ret)
		goto err_free_domain;

	group = iommu_group_get(dev);
	if (!group) {
		group = iommu_group_alloc();
		if (IS_ERR(group)) {
			dev_err(dev, "Failed to allocate IOMMU group\n");
			goto err_put_cookie;
		}

		ret = iommu_group_add_device(group, dev);
		iommu_group_put(group);
		if (ret) {
			dev_err(dev, "failed to add device to IOMMU group\n");
			goto err_put_cookie;
		}
	}
	/*
	 * Attach virtual iommu device, sub iommu device can share the same
	 * mapping with it.
	 */
	ret = rockchip_drm_dma_attach_device(drm_dev, dev);
	if (ret)
		goto err_group_remove_device;

	/* Try to bind all sub drivers. */
	ret = component_bind_all(dev, drm_dev);
	if (ret)
		goto err_detach_device;

	/*
	 * All components are now added, we can publish the connector sysfs
	 * entries to userspace.  This will generate hotplug events and so
	 * userspace will expect to be able to access DRM at this point.
	 */
	list_for_each_entry(connector, &drm_dev->mode_config.connector_list,
			head) {
		ret = drm_connector_register(connector);
		if (ret) {
			dev_err(drm_dev->dev,
				"[CONNECTOR:%d:%s] drm_connector_register failed: %d\n",
				connector->base.id,
				connector->name, ret);
			goto err_unbind;
		}
	}

	/* init kms poll for handling hpd */
	drm_kms_helper_poll_init(drm_dev);

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = true, we can use the vblank feature.
	 */
	drm_dev->irq_enabled = true;

	ret = drm_vblank_init(drm_dev, ROCKCHIP_MAX_CRTC);
	if (ret)
		goto err_kms_helper_poll_fini;

	/*
	 * with vblank_disable_allowed = true, vblank interrupt will be disabled
	 * by drm timer once a current process gives up ownership of
	 * vblank event.(after drm_vblank_put function is called)
	 */
	drm_dev->vblank_disable_allowed = true;

	drm_mode_config_reset(drm_dev);

	show_loader_logo(drm_dev);

	ret = rockchip_drm_fbdev_init(drm_dev);
	if (ret)
		goto err_vblank_cleanup;

	drm_dev->mode_config.allow_fb_modifiers = true;

	return 0;
err_vblank_cleanup:
	drm_vblank_cleanup(drm_dev);
err_kms_helper_poll_fini:
	drm_kms_helper_poll_fini(drm_dev);
err_unbind:
	component_unbind_all(dev, drm_dev);
err_detach_device:
	rockchip_drm_dma_detach_device(drm_dev, dev);
err_group_remove_device:
	iommu_group_remove_device(dev);
err_put_cookie:
	iommu_put_dma_cookie(private->domain);
err_free_domain:
	iommu_domain_free(private->domain);
err_config_cleanup:
	drm_mode_config_cleanup(drm_dev);
	drm_dev->dev_private = NULL;
	return ret;
}

static int rockchip_drm_unload(struct drm_device *drm_dev)
{
	struct device *dev = drm_dev->dev;
	struct rockchip_drm_private *private = drm_dev->dev_private;

	rockchip_drm_fbdev_fini(drm_dev);
	drm_vblank_cleanup(drm_dev);
	drm_kms_helper_poll_fini(drm_dev);
	component_unbind_all(dev, drm_dev);
	rockchip_drm_dma_detach_device(drm_dev, dev);
	iommu_group_remove_device(dev);
	iommu_put_dma_cookie(private->domain);
	iommu_domain_free(private->domain);
	drm_mode_config_cleanup(drm_dev);
	drm_dev->dev_private = NULL;

	return 0;
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

void rockchip_drm_lastclose(struct drm_device *dev)
{
	struct rockchip_drm_private *priv = dev->dev_private;

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
	DRM_IOCTL_DEF_DRV(ROCKCHIP_RGA_GET_VER, rockchip_rga_get_ver_ioctl,
			  DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_RGA_SET_CMDLIST,
			  rockchip_rga_set_cmdlist_ioctl,
			  DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_RGA_EXEC, rockchip_rga_exec_ioctl,
			  DRM_AUTH | DRM_RENDER_ALLOW),
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
	.load			= rockchip_drm_load,
	.unload			= rockchip_drm_unload,
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
	.gem_prime_import_sg_table = rockchip_gem_prime_import_sg_table,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_get_sg_table	= rockchip_gem_prime_get_sg_table,
	.gem_prime_vmap		= rockchip_gem_prime_vmap,
	.gem_prime_vunmap	= rockchip_gem_prime_vunmap,
	.gem_prime_mmap		= rockchip_gem_mmap_buf,
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
void rockchip_drm_fb_suspend(struct drm_device *drm)
{
	struct rockchip_drm_private *priv = drm->dev_private;

	console_lock();
	drm_fb_helper_set_suspend(priv->fbdev_helper, 1);
	console_unlock();
}

void rockchip_drm_fb_resume(struct drm_device *drm)
{
	struct rockchip_drm_private *priv = drm->dev_private;

	console_lock();
	drm_fb_helper_set_suspend(priv->fbdev_helper, 0);
	console_unlock();
}

static int rockchip_drm_sys_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct rockchip_drm_private *priv = drm->dev_private;

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

static int rockchip_drm_bind(struct device *dev)
{
	struct drm_device *drm;
	int ret;

	drm = drm_dev_alloc(&rockchip_drm_driver, dev);
	if (!drm)
		return -ENOMEM;

	ret = drm_dev_set_unique(drm, "%s", dev_name(dev));
	if (ret)
		goto err_free;

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto err_free;

	dev_set_drvdata(dev, drm);

	return 0;

err_free:
	drm_dev_unref(drm);
	return ret;
}

static void rockchip_drm_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	drm_dev_unregister(drm);
	drm_dev_unref(drm);
	dev_set_drvdata(dev, NULL);
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

	if (!np)
		return -ENODEV;
	/*
	 * Bind the crtc ports first, so that
	 * drm_of_find_possible_crtcs called from encoder .bind callbacks
	 * works as expected.
	 */
	for (i = 0;; i++) {
		port = of_parse_phandle(np, "ports", i);
		if (!port)
			break;

		if (!of_device_is_available(port->parent)) {
			of_node_put(port);
			continue;
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

	return component_master_add_with_match(dev, &rockchip_drm_ops, match);
}

static int rockchip_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &rockchip_drm_ops);

	return 0;
}

static const struct of_device_id rockchip_drm_dt_ids[] = {
	{ .compatible = "rockchip,display-subsystem", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rockchip_drm_dt_ids);

static struct platform_driver rockchip_drm_platform_driver = {
	.probe = rockchip_drm_platform_probe,
	.remove = rockchip_drm_platform_remove,
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
