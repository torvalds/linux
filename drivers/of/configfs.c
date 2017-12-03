/*
 * Configfs entries for device-tree
 *
 * Copyright (C) 2013 - Pantelis Antoniou <panto@antoniou-consulting.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/ctype.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/configfs.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/limits.h>
#include <linux/file.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/sizes.h>

#include "of_private.h"

struct cfs_overlay_item {
	struct config_item	item;

	char			path[PATH_MAX];

	const struct firmware	*fw;
	struct device_node	*overlay;
	int			ov_id;

	void			*dtbo;
	int			dtbo_size;
};

static int create_overlay(struct cfs_overlay_item *overlay, void *blob)
{
	int ovcs_id, err;

	/* unflatten the tree */
	of_fdt_unflatten_tree(blob, NULL, &overlay->overlay);
	if (overlay->overlay == NULL) {
		pr_err("%s: failed to unflatten tree\n", __func__);
		err = -EINVAL;
		goto out_err;
	}
	pr_debug("%s: unflattened OK\n", __func__);

	/* mark it as detached */
	of_node_set_flag(overlay->overlay, OF_DETACHED);

	/* perform resolution */
	err = of_resolve_phandles(overlay->overlay);
	if (err != 0) {
		pr_err("%s: Failed to resolve tree\n", __func__);
		goto out_err;
	}
	pr_debug("%s: resolved OK\n", __func__);

	ovcs_id = 0;
	err = of_overlay_apply(overlay->overlay, ovcs_id);
	if (err < 0) {
		pr_err("%s: Failed to create overlay (err=%d)\n",
				__func__, err);
		goto out_err;
	}
	overlay->ov_id = err;

out_err:
	return err;
}

static inline struct cfs_overlay_item *to_cfs_overlay_item(
		struct config_item *item)
{
	return item ? container_of(item, struct cfs_overlay_item, item) : NULL;
}

static ssize_t cfs_overlay_item_path_show(struct config_item *item,
		char *page)
{
	struct cfs_overlay_item *overlay = to_cfs_overlay_item(item);
	return sprintf(page, "%s\n", overlay->path);
}

static ssize_t cfs_overlay_item_path_store(struct config_item *item,
		const char *page, size_t count)
{
	struct cfs_overlay_item *overlay = to_cfs_overlay_item(item);
	const char *p = page;
	char *s;
	int err;

	/* if it's set do not allow changes */
	if (overlay->path[0] != '\0' || overlay->dtbo_size > 0)
		return -EPERM;

	/* copy to path buffer (and make sure it's always zero terminated */
	count = snprintf(overlay->path, sizeof(overlay->path) - 1, "%s", p);
	overlay->path[sizeof(overlay->path) - 1] = '\0';

	/* strip trailing newlines */
	s = overlay->path + strlen(overlay->path);
	while (s > overlay->path && *--s == '\n')
		*s = '\0';

	pr_debug("%s: path is '%s'\n", __func__, overlay->path);

	err = request_firmware(&overlay->fw, overlay->path, NULL);
	if (err != 0)
		goto out_err;

	err = create_overlay(overlay, (void *)overlay->fw->data);
	if (err != 0)
		goto out_err;

	return count;

out_err:

	release_firmware(overlay->fw);
	overlay->fw = NULL;

	overlay->path[0] = '\0';
	return err;
}

static ssize_t cfs_overlay_item_status_show(struct config_item *item,
		char *page)
{
	struct cfs_overlay_item *overlay = to_cfs_overlay_item(item);

	return sprintf(page, "%s\n",
			overlay->ov_id >= 0 ? "applied" : "unapplied");
}

CONFIGFS_ATTR(cfs_overlay_item_, path);
CONFIGFS_ATTR_RO(cfs_overlay_item_, status);

static struct configfs_attribute *cfs_overlay_attrs[] = {
	&cfs_overlay_item_attr_path,
	&cfs_overlay_item_attr_status,
	NULL,
};

ssize_t cfs_overlay_item_dtbo_read(struct config_item *item,
		void *buf, size_t max_count)
{
	struct cfs_overlay_item *overlay = to_cfs_overlay_item(item);

	pr_debug("%s: buf=%p max_count=%zu\n", __func__,
			buf, max_count);

	if (overlay->dtbo == NULL)
		return 0;

	/* copy if buffer provided */
	if (buf != NULL) {
		/* the buffer must be large enough */
		if (overlay->dtbo_size > max_count)
			return -ENOSPC;

		memcpy(buf, overlay->dtbo, overlay->dtbo_size);
	}

	return overlay->dtbo_size;
}

ssize_t cfs_overlay_item_dtbo_write(struct config_item *item,
		const void *buf, size_t count)
{
	struct cfs_overlay_item *overlay = to_cfs_overlay_item(item);
	int err;

	/* if it's set do not allow changes */
	if (overlay->path[0] != '\0' || overlay->dtbo_size > 0)
		return -EPERM;

	/* copy the contents */
	overlay->dtbo = kmemdup(buf, count, GFP_KERNEL);
	if (overlay->dtbo == NULL)
		return -ENOMEM;

	overlay->dtbo_size = count;

	err = create_overlay(overlay, overlay->dtbo);
	if (err != 0)
		goto out_err;

	return count;

out_err:
	kfree(overlay->dtbo);
	overlay->dtbo = NULL;
	overlay->dtbo_size = 0;

	return err;
}

CONFIGFS_BIN_ATTR(cfs_overlay_item_, dtbo, NULL, SZ_1M);

static struct configfs_bin_attribute *cfs_overlay_bin_attrs[] = {
	&cfs_overlay_item_attr_dtbo,
	NULL,
};

static void cfs_overlay_release(struct config_item *item)
{
	struct cfs_overlay_item *overlay = to_cfs_overlay_item(item);

	if (overlay->ov_id >= 0)
		of_overlay_remove(&overlay->ov_id);
	if (overlay->fw)
		release_firmware(overlay->fw);
	/* kfree with NULL is safe */
	kfree(overlay->dtbo);
	kfree(overlay);
}

static struct configfs_item_operations cfs_overlay_item_ops = {
	.release	= cfs_overlay_release,
};

static struct config_item_type cfs_overlay_type = {
	.ct_item_ops	= &cfs_overlay_item_ops,
	.ct_attrs	= cfs_overlay_attrs,
	.ct_bin_attrs	= cfs_overlay_bin_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_item *cfs_overlay_group_make_item(
		struct config_group *group, const char *name)
{
	struct cfs_overlay_item *overlay;

	overlay = kzalloc(sizeof(*overlay), GFP_KERNEL);
	if (!overlay)
		return ERR_PTR(-ENOMEM);
	overlay->ov_id = -1;

	config_item_init_type_name(&overlay->item, name, &cfs_overlay_type);
	return &overlay->item;
}

static void cfs_overlay_group_drop_item(struct config_group *group,
		struct config_item *item)
{
	struct cfs_overlay_item *overlay = to_cfs_overlay_item(item);

	config_item_put(&overlay->item);
}

static struct configfs_group_operations overlays_ops = {
	.make_item	= cfs_overlay_group_make_item,
	.drop_item	= cfs_overlay_group_drop_item,
};

static struct config_item_type overlays_type = {
	.ct_group_ops   = &overlays_ops,
	.ct_owner       = THIS_MODULE,
};

static struct configfs_group_operations of_cfs_ops = {
	/* empty - we don't allow anything to be created */
};

static struct config_item_type of_cfs_type = {
	.ct_group_ops   = &of_cfs_ops,
	.ct_owner       = THIS_MODULE,
};

struct config_group of_cfs_overlay_group;

static struct configfs_subsystem of_cfs_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "device-tree",
			.ci_type = &of_cfs_type,
		},
	},
	.su_mutex = __MUTEX_INITIALIZER(of_cfs_subsys.su_mutex),
};

static int __init of_cfs_init(void)
{
	int ret;

	pr_info("%s\n", __func__);

	config_group_init(&of_cfs_subsys.su_group);
	config_group_init_type_name(&of_cfs_overlay_group, "overlays",
			&overlays_type);
	configfs_add_default_group(&of_cfs_overlay_group,
			&of_cfs_subsys.su_group);

	ret = configfs_register_subsystem(&of_cfs_subsys);
	if (ret != 0) {
		pr_err("%s: failed to register subsys\n", __func__);
		goto out;
	}
	pr_info("%s: OK\n", __func__);
out:
	return ret;
}
late_initcall(of_cfs_init);
