/*
 * Overlay manager that allows to apply list of overlays from DT entry
 *
 * Copyright (C) 2016 Google, Inc.
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

static char *of_overlay_dt_entry;
module_param_named(overlay_dt_entry, of_overlay_dt_entry, charp, 0644);

static char *of_overlay_dt_apply;
DEFINE_MUTEX(of_overlay_mgr_mutex);

static int of_overlay_mgr_apply_overlay(struct device_node *onp)
{
	int ret;

	ret = of_overlay_create(onp);
	if (ret < 0) {
		pr_err("overlay_mgr: fail to create overlay: %d\n", ret);
		of_node_put(onp);
		return ret;
	}
	pr_info("overlay_mgr: %s overlay applied\n", onp->name);
	return 0;
}

static int of_overlay_mgr_apply_dt(struct device *dev, char *dt_entry)
{
	struct device_node *enp = dev->of_node;
	struct device_node *next;
	struct device_node *prev = NULL;

	if (!enp) {
		pr_err("overlay_mgr: no dt entry\n");
		return -ENODEV;
	}

	enp = of_get_child_by_name(enp, dt_entry);
	if (!enp) {
		pr_err("overlay_mgr: dt entry %s not found\n", dt_entry);
		return -ENODEV;
	}
	pr_info("overlay_mgr: apply %s dt entry\n", enp->name);
	while ((next = of_get_next_available_child(enp, prev)) != NULL) {
		if (strncmp(next->name, "overlay", 7) == 0)
			of_overlay_mgr_apply_overlay(next);
		prev = next;
	}
	return 0;
}

static ssize_t current_overlay_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	size_t len;

	mutex_lock(&of_overlay_mgr_mutex);
	if (!of_overlay_dt_apply) {
		mutex_unlock(&of_overlay_mgr_mutex);
		return 0;
	}
	len = strlen(of_overlay_dt_apply);
	if (len >= PAGE_SIZE)
		len = PAGE_SIZE - 1;
	memcpy(buf, of_overlay_dt_apply, len + 1);
	mutex_unlock(&of_overlay_mgr_mutex);
	return len;
}

static ssize_t current_overlay_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	mutex_lock(&of_overlay_mgr_mutex);
	kfree(of_overlay_dt_apply);
	of_overlay_dt_apply = kmalloc(size, GFP_KERNEL);
	if (!of_overlay_dt_apply) {
		pr_err("overlay_mgr: fail to allocate memory\n");
		mutex_unlock(&of_overlay_mgr_mutex);
		return 0;
	}
	memcpy(of_overlay_dt_apply, buf, size);
	of_overlay_dt_apply[size - 1] = '\0';

	if (of_overlay_mgr_apply_dt(dev, of_overlay_dt_apply)) {
		kfree(of_overlay_dt_apply);
		of_overlay_dt_apply = NULL;
		size = 0;
	}
	mutex_unlock(&of_overlay_mgr_mutex);
	return size;
}

static DEVICE_ATTR(current_overlay, 0644, current_overlay_show,
		   current_overlay_store);

static int of_overlay_mgr_probe(struct platform_device *pdev)
{
	if (device_create_file(&pdev->dev, &dev_attr_current_overlay))
		pr_err("overlay_mgr: fail to register apply entry\n");

	if (!of_overlay_dt_entry)
		return 0;
	of_overlay_mgr_apply_dt(&pdev->dev, of_overlay_dt_entry);
	return 0;
}

static const struct of_device_id of_overlay_mgr_match[] = {
	{ .compatible = "linux,overlay_manager", },
	{}
};

static struct platform_driver of_overlay_mgr_driver = {
	.probe	= of_overlay_mgr_probe,
	.driver	= {
		.name = "overlay_manager",
		.of_match_table = of_match_ptr(of_overlay_mgr_match),
	},
};

static int __init of_overlay_mgr_init(void)
{
	return platform_driver_register(&of_overlay_mgr_driver);
}

postcore_initcall(of_overlay_mgr_init);
