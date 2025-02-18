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

	return config;
}
EXPORT_SYMBOL_IF_KUNIT(vkms_config_create);

struct vkms_config *vkms_config_default_create(bool enable_cursor,
					       bool enable_writeback,
					       bool enable_overlay)
{
	struct vkms_config *config;

	config = vkms_config_create(DEFAULT_DEVICE_NAME);
	if (IS_ERR(config))
		return config;

	config->cursor = enable_cursor;
	config->writeback = enable_writeback;
	config->overlay = enable_overlay;

	return config;
}
EXPORT_SYMBOL_IF_KUNIT(vkms_config_default_create);

void vkms_config_destroy(struct vkms_config *config)
{
	kfree_const(config->dev_name);
	kfree(config);
}
EXPORT_SYMBOL_IF_KUNIT(vkms_config_destroy);

static int vkms_config_show(struct seq_file *m, void *data)
{
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *dev = entry->dev;
	struct vkms_device *vkmsdev = drm_device_to_vkms_device(dev);
	const char *dev_name;

	dev_name = vkms_config_get_device_name((struct vkms_config *)vkmsdev->config);
	seq_printf(m, "dev_name=%s\n", dev_name);
	seq_printf(m, "writeback=%d\n", vkmsdev->config->writeback);
	seq_printf(m, "cursor=%d\n", vkmsdev->config->cursor);
	seq_printf(m, "overlay=%d\n", vkmsdev->config->overlay);

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
