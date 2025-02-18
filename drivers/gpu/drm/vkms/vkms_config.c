// SPDX-License-Identifier: GPL-2.0+

#include <linux/slab.h>

#include <drm/drm_print.h>
#include <drm/drm_debugfs.h>
#include <kunit/visibility.h>

#include "vkms_config.h"

struct vkms_config *vkms_config_create(const char *dev_name)
{
	struct vkms_config *config;

	config = kzalloc(sizeof(*config), GFP_KERNEL);
	if (!config)
		return ERR_PTR(-ENOMEM);

	config->dev_name = kstrdup_const(dev_name, GFP_KERNEL);
	if (!config->dev_name) {
		kfree(config);
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&config->planes);
	INIT_LIST_HEAD(&config->crtcs);

	return config;
}
EXPORT_SYMBOL_IF_KUNIT(vkms_config_create);

struct vkms_config *vkms_config_default_create(bool enable_cursor,
					       bool enable_writeback,
					       bool enable_overlay)
{
	struct vkms_config *config;
	struct vkms_config_plane *plane_cfg;
	struct vkms_config_crtc *crtc_cfg;
	int n;

	config = vkms_config_create(DEFAULT_DEVICE_NAME);
	if (IS_ERR(config))
		return config;

	plane_cfg = vkms_config_create_plane(config);
	if (IS_ERR(plane_cfg))
		goto err_alloc;
	vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_PRIMARY);

	crtc_cfg = vkms_config_create_crtc(config);
	if (IS_ERR(crtc_cfg))
		goto err_alloc;
	vkms_config_crtc_set_writeback(crtc_cfg, enable_writeback);

	if (enable_overlay) {
		for (n = 0; n < NUM_OVERLAY_PLANES; n++) {
			plane_cfg = vkms_config_create_plane(config);
			if (IS_ERR(plane_cfg))
				goto err_alloc;
			vkms_config_plane_set_type(plane_cfg,
						   DRM_PLANE_TYPE_OVERLAY);
		}
	}

	if (enable_cursor) {
		plane_cfg = vkms_config_create_plane(config);
		if (IS_ERR(plane_cfg))
			goto err_alloc;
		vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_CURSOR);
	}

	return config;

err_alloc:
	vkms_config_destroy(config);
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL_IF_KUNIT(vkms_config_default_create);

void vkms_config_destroy(struct vkms_config *config)
{
	struct vkms_config_plane *plane_cfg, *plane_tmp;
	struct vkms_config_crtc *crtc_cfg, *crtc_tmp;

	list_for_each_entry_safe(plane_cfg, plane_tmp, &config->planes, link)
		vkms_config_destroy_plane(plane_cfg);

	list_for_each_entry_safe(crtc_cfg, crtc_tmp, &config->crtcs, link)
		vkms_config_destroy_crtc(config, crtc_cfg);

	kfree_const(config->dev_name);
	kfree(config);
}
EXPORT_SYMBOL_IF_KUNIT(vkms_config_destroy);

static bool valid_plane_number(const struct vkms_config *config)
{
	struct drm_device *dev = config->dev ? &config->dev->drm : NULL;
	size_t n_planes;

	n_planes = list_count_nodes((struct list_head *)&config->planes);
	if (n_planes <= 0 || n_planes >= 32) {
		drm_info(dev, "The number of planes must be between 1 and 31\n");
		return false;
	}

	return true;
}

static bool valid_plane_type(const struct vkms_config *config)
{
	struct drm_device *dev = config->dev ? &config->dev->drm : NULL;
	struct vkms_config_plane *plane_cfg;
	bool has_primary_plane = false;
	bool has_cursor_plane = false;

	vkms_config_for_each_plane(config, plane_cfg) {
		enum drm_plane_type type;

		type = vkms_config_plane_get_type(plane_cfg);

		if (type == DRM_PLANE_TYPE_PRIMARY) {
			if (has_primary_plane) {
				drm_info(dev, "Multiple primary planes\n");
				return false;
			}

			has_primary_plane = true;
		} else if (type == DRM_PLANE_TYPE_CURSOR) {
			if (has_cursor_plane) {
				drm_info(dev, "Multiple cursor planes\n");
				return false;
			}

			has_cursor_plane = true;
		}
	}

	if (!has_primary_plane) {
		drm_info(dev, "Primary plane not found\n");
		return false;
	}

	return true;
}

static bool valid_crtc_number(const struct vkms_config *config)
{
	struct drm_device *dev = config->dev ? &config->dev->drm : NULL;
	size_t n_crtcs;

	n_crtcs = list_count_nodes((struct list_head *)&config->crtcs);
	if (n_crtcs <= 0 || n_crtcs >= 32) {
		drm_info(dev, "The number of CRTCs must be between 1 and 31\n");
		return false;
	}

	return true;
}

bool vkms_config_is_valid(const struct vkms_config *config)
{
	if (!valid_plane_number(config))
		return false;

	if (!valid_crtc_number(config))
		return false;

	if (!valid_plane_type(config))
		return false;

	return true;
}
EXPORT_SYMBOL_IF_KUNIT(vkms_config_is_valid);

static int vkms_config_show(struct seq_file *m, void *data)
{
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *dev = entry->dev;
	struct vkms_device *vkmsdev = drm_device_to_vkms_device(dev);
	const char *dev_name;
	struct vkms_config_plane *plane_cfg;
	struct vkms_config_crtc *crtc_cfg;

	dev_name = vkms_config_get_device_name((struct vkms_config *)vkmsdev->config);
	seq_printf(m, "dev_name=%s\n", dev_name);

	vkms_config_for_each_plane(vkmsdev->config, plane_cfg) {
		seq_puts(m, "plane:\n");
		seq_printf(m, "\ttype=%d\n",
			   vkms_config_plane_get_type(plane_cfg));
	}

	vkms_config_for_each_crtc(vkmsdev->config, crtc_cfg) {
		seq_puts(m, "crtc:\n");
		seq_printf(m, "\twriteback=%d\n",
			   vkms_config_crtc_get_writeback(crtc_cfg));
	}

	return 0;
}

static const struct drm_debugfs_info vkms_config_debugfs_list[] = {
	{ "vkms_config", vkms_config_show, 0 },
};

void vkms_config_register_debugfs(struct vkms_device *vkms_device)
{
	drm_debugfs_add_files(&vkms_device->drm, vkms_config_debugfs_list,
			      ARRAY_SIZE(vkms_config_debugfs_list));
}

struct vkms_config_plane *vkms_config_create_plane(struct vkms_config *config)
{
	struct vkms_config_plane *plane_cfg;

	plane_cfg = kzalloc(sizeof(*plane_cfg), GFP_KERNEL);
	if (!plane_cfg)
		return ERR_PTR(-ENOMEM);

	plane_cfg->config = config;
	vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_OVERLAY);

	list_add_tail(&plane_cfg->link, &config->planes);

	return plane_cfg;
}
EXPORT_SYMBOL_IF_KUNIT(vkms_config_create_plane);

void vkms_config_destroy_plane(struct vkms_config_plane *plane_cfg)
{
	list_del(&plane_cfg->link);
	kfree(plane_cfg);
}
EXPORT_SYMBOL_IF_KUNIT(vkms_config_destroy_plane);

struct vkms_config_crtc *vkms_config_create_crtc(struct vkms_config *config)
{
	struct vkms_config_crtc *crtc_cfg;

	crtc_cfg = kzalloc(sizeof(*crtc_cfg), GFP_KERNEL);
	if (!crtc_cfg)
		return ERR_PTR(-ENOMEM);

	crtc_cfg->config = config;
	vkms_config_crtc_set_writeback(crtc_cfg, false);

	list_add_tail(&crtc_cfg->link, &config->crtcs);

	return crtc_cfg;
}
EXPORT_SYMBOL_IF_KUNIT(vkms_config_create_crtc);

void vkms_config_destroy_crtc(struct vkms_config *config,
			      struct vkms_config_crtc *crtc_cfg)
{
	list_del(&crtc_cfg->link);
	kfree(crtc_cfg);
}
EXPORT_SYMBOL_IF_KUNIT(vkms_config_destroy_crtc);
