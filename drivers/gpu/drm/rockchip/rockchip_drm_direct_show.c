// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 * Author: Sandy Huang <hjc@rock-chips.com>
 */
#include <linux/dma-buf-cache.h>
#include <linux/fdtable.h>
#include <drm/drm_atomic_uapi.h>

#include "../drm_internal.h"
#include "rockchip_drm_direct_show.h"

static int drm_ds_debug;
#define DRM_DS_DBG(format, ...) do {	\
	if (drm_ds_debug)	\
		pr_info("DRM_DS: %s(%d): " format, __func__, __LINE__, ## __VA_ARGS__);	\
	} while (0)

#define DRM_DS_ERR(format, ...) \
	pr_info("ERR: DRM_DS: %s(%d): " format, __func__, __LINE__, ## __VA_ARGS__)

struct drm_device *rockchip_drm_get_dev(void)
{
	int i;
	char *name = "rockchip";

	for (i = 0; i < 64; i++) {
		struct drm_minor *minor;

		minor = drm_minor_acquire(i + DRM_MINOR_PRIMARY);
		if (IS_ERR(minor))
			continue;
		if (!minor->dev || !minor->dev->driver ||
		    !minor->dev->driver->name)
			continue;
		if (!name)
			return minor->dev;
		if (!strcmp(name, minor->dev->driver->name))
			return minor->dev;
	}

	return NULL;
}

static int rockchip_drm_direct_show_alloc_fb(struct drm_device *drm,
					     struct rockchip_drm_direct_show_buffer *buffer)
{
	struct drm_gem_object *obj;
	struct drm_gem_object *objs[ROCKCHIP_MAX_FB_BUFFER];
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	struct drm_framebuffer *fb;
	const struct drm_format_info *format_info = drm_format_info(buffer->pixel_format);

	mode_cmd.offsets[0] = 0;
	mode_cmd.width = buffer->width;
	mode_cmd.height = buffer->height;
	mode_cmd.pitches[0] = buffer->pitch[0];
	mode_cmd.pixel_format = buffer->pixel_format;
	obj = &buffer->rk_gem_obj->base;
	objs[0] = obj;

	if (format_info->is_yuv) {
		mode_cmd.offsets[1] = buffer->width * buffer->height;
		mode_cmd.pitches[1] = mode_cmd.pitches[0];
		objs[1] = obj;
	}

	fb = rockchip_fb_alloc(drm, &mode_cmd, objs, format_info->num_planes);
	if (IS_ERR_OR_NULL(fb))
		return -ENOMEM;

	buffer->fb = fb;

	return 0;
}

int rockchip_drm_direct_show_alloc_buffer(struct drm_device *drm,
					  struct rockchip_drm_direct_show_buffer *buffer)
{
	u32 min_pitch;
	const struct drm_format_info *format_info;
	struct drm_mode_create_dumb args;
	struct rockchip_gem_object *rk_obj;
	struct drm_gem_object *obj;
	struct dma_buf *dmabuf;
	int dmabuf_fd;

	args.width = buffer->width;
	args.height = buffer->height;
	format_info = drm_format_info(buffer->pixel_format);
	args.bpp = rockchip_drm_get_bpp(format_info);
	min_pitch = args.width * DIV_ROUND_UP(args.bpp, 8);
	args.pitch = ALIGN(min_pitch, 64);
	args.size = args.pitch * args.height;
	args.flags = buffer->flag;

	if (format_info->is_yuv) {
		int bpp = 0;

		bpp = format_info->cpp[1] * 8;
		min_pitch = args.width * DIV_ROUND_UP(bpp, 8);
		min_pitch = ALIGN(min_pitch, 64);
		args.size += min_pitch * args.height / format_info->hsub / format_info->vsub;
	}
	/* create a gem obj with kmap flag */
	rk_obj = rockchip_gem_create_object(drm, args.size, true, args.flags);
	if (IS_ERR(rk_obj)) {
		DRM_DS_ERR("create rk_obj failed\n");
		return -ENOMEM;
	}
	obj = &rk_obj->base;

	buffer->bpp = args.bpp;
	buffer->pitch[0] = args.pitch;
	buffer->vir_addr[0] = rk_obj->kvaddr;
	buffer->phy_addr[0] = rk_obj->dma_handle;
	buffer->rk_gem_obj = rk_obj;
	if (format_info->is_yuv) {
		buffer->vir_addr[1] = buffer->vir_addr[0] + buffer->width * buffer->height;
		buffer->pitch[1] = buffer->pitch[0];
		buffer->phy_addr[1] = buffer->phy_addr[0] + buffer->width * buffer->height;
	}

	/* to get drm fb */
	rockchip_drm_direct_show_alloc_fb(drm, buffer);

	/* to get dma buffer fd */
	mutex_lock(&drm->object_name_lock);
	dmabuf = drm->driver->gem_prime_export(obj, 0);
	if (IS_ERR(dmabuf)) {
		mutex_unlock(&drm->object_name_lock);
		goto err_gem_free;
	}
	obj->dma_buf = dmabuf;
	get_dma_buf(obj->dma_buf);
	drm_gem_dmabuf_release(obj->dma_buf);
	mutex_unlock(&drm->object_name_lock);

	dmabuf_fd = dma_buf_fd(dmabuf, 0);
	if (dmabuf_fd < 0) {
		DRM_DS_ERR("failed dma_buf_fd, ret %d\n", dmabuf_fd);
		goto err_free_dmabuf;
	}
	buffer->dmabuf_fd = dmabuf_fd;

	DRM_DS_DBG("alloc buffer: 0x%p, dma buf fd:%d, args.pitch:%d\n", buffer->rk_gem_obj, dmabuf_fd, args.pitch);

	return 0;

err_free_dmabuf:
	dma_buf_put(dmabuf);
err_gem_free:
	drm_gem_object_put(&rk_obj->base);

	return -ENOMEM;
}

void rockchip_drm_direct_show_free_buffer(struct drm_device *drm,
					  struct rockchip_drm_direct_show_buffer *buffer)
{
	struct drm_gem_object *obj = &buffer->rk_gem_obj->base;

	DRM_DS_DBG("free buffer: 0x%p\n", buffer->rk_gem_obj);

	mutex_lock(&drm->object_name_lock);
	if (obj->dma_buf) {
		dma_buf_put(obj->dma_buf);
		obj->dma_buf = NULL;
	}
	mutex_unlock(&drm->object_name_lock);

	drm_gem_object_put(obj);
}

struct drm_plane *rockchip_drm_direct_show_get_plane(struct drm_device *drm, const char *name)
{
	struct drm_plane *plane;

	drm_for_each_plane(plane, drm) {
		if (!strncmp(plane->name, name, DRM_PROP_NAME_LEN))
			break;
	}
	if (!plane) {
		DRM_DS_ERR("failed to find plane:%s!\n", name);
		return NULL;
	}

	DRM_DS_DBG("get plane[%s] success\n", plane->name);

	return plane;
}

struct drm_crtc *rockchip_drm_direct_show_get_crtc(struct drm_device *drm, const char *name)
{
	struct drm_crtc *crtc = NULL;
	bool crtc_active = false;

	drm_for_each_crtc(crtc, drm) {
		if (name == NULL) {
			if (crtc->state && crtc->state->active) {
				crtc_active = true;
				break;
			}
		} else {
			if (crtc->state && crtc->state->active &&
			    !strncmp(crtc->name, name, DRM_PROP_NAME_LEN)) {
				crtc_active = true;
				break;
			}
		}
	}

	if (crtc_active == false) {
		DRM_DS_ERR("failed to find active crtc\n");
		return NULL;
	}
	DRM_DS_DBG("get crtc[%s] success\n", crtc->name);

	return crtc;
}

static int
rockchip_drm_direct_show_set_property_value(struct drm_mode_object *obj,
					    struct drm_property *property,
					    uint64_t val)
{
	int i;

	for (i = 0; i < obj->properties->count; i++) {
		if (obj->properties->properties[i] == property) {
			obj->properties->values[i] = val;
			return 0;
		}
	}

	return -EINVAL;
}

static struct drm_property *
rockchip_drm_direct_show_find_prop(struct drm_device *dev,
				   struct drm_mode_object *obj,
				   char *prop_name)
{
	int i = 0;

	if (!obj->properties)
		return NULL;

	for (i = 0; i < obj->properties->count; i++) {
		struct drm_property *prop = obj->properties->properties[i];

		if (!strncmp(prop->name, prop_name, DRM_PROP_NAME_LEN))
			return prop;
	}

	return NULL;
}

int rockchip_drm_direct_show_commit(struct drm_device *drm,
				    struct rockchip_drm_direct_show_commit_info *commit_info)
{
	int ret = 0;
	struct drm_plane *plane = commit_info->plane;
	struct drm_crtc *crtc = commit_info->crtc;
	struct drm_framebuffer *fb = commit_info->buffer->fb;
	struct drm_mode_config *conf = &drm->mode_config;
	struct drm_property *zpos_prop;

	/*setplane overlay zpos top*/
	zpos_prop = rockchip_drm_direct_show_find_prop(drm, &plane->base, "zpos");
	if (!zpos_prop)
		DRM_DS_ERR("failed to find plane zpos prop, ret:%d\n", ret);

	drm_modeset_lock_all(drm);
	/* set the max zpos value */
	if (commit_info->top_zpos && zpos_prop) {
		ret = rockchip_drm_direct_show_set_property_value(&plane->base,
								  zpos_prop,
								  zpos_prop->values[1]);
		if (ret)
			DRM_DS_ERR("failed to set plane zpos prop, ret:%d\n", ret);
		plane->state->zpos = zpos_prop->values[1];
	}
	ret = plane->funcs->update_plane(plane, crtc, fb,
					 commit_info->dst_x, commit_info->dst_y,
					 commit_info->dst_w, commit_info->dst_h,
					 commit_info->src_x << 16,
					 commit_info->src_y << 16,
					 commit_info->src_w << 16,
					 commit_info->src_h << 16,
					 conf->acquire_ctx);
	drm_modeset_unlock_all(drm);

	if (ret)
		return ret;

	DRM_DS_DBG("commit success: plane[%s], crtc[%s], src[%dx%d@%dx%d], dst[%dx%d@%dx%d]\n",
		   plane->name, crtc->name,
		   commit_info->src_w, commit_info->src_h,
		   commit_info->src_x, commit_info->src_y,
		   commit_info->dst_w, commit_info->dst_h,
		   commit_info->dst_x, commit_info->dst_y);

	return ret;
}

int rockchip_drm_direct_show_disable_plane(struct drm_device *drm, struct drm_plane *plane)
{
	int ret = 0;
	struct drm_mode_config *conf = &drm->mode_config;

	DRM_DS_DBG("disable plane: %s\n", plane->name);
	drm_modeset_lock_all(drm);
	ret = plane->funcs->disable_plane(plane, conf->acquire_ctx);
	drm_modeset_unlock_all(drm);

	return ret;
}
