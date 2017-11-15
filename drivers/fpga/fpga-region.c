/*
 * FPGA Region - Device Tree support for FPGA programming under Linux
 *
 *  Copyright (C) 2013-2016 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/fpga/fpga-bridge.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

/**
 * struct fpga_region - FPGA Region structure
 * @dev: FPGA Region device
 * @mutex: enforces exclusive reference to region
 * @bridge_list: list of FPGA bridges specified in region
 * @info: fpga image specific information
 */
struct fpga_region {
	struct device dev;
	struct mutex mutex; /* for exclusive reference to region */
	struct list_head bridge_list;
	struct fpga_image_info *info;
};

#define to_fpga_region(d) container_of(d, struct fpga_region, dev)

static DEFINE_IDA(fpga_region_ida);
static struct class *fpga_region_class;

static const struct of_device_id fpga_region_of_match[] = {
	{ .compatible = "fpga-region", },
	{},
};
MODULE_DEVICE_TABLE(of, fpga_region_of_match);

static int fpga_region_of_node_match(struct device *dev, const void *data)
{
	return dev->of_node == data;
}

/**
 * fpga_region_find - find FPGA region
 * @np: device node of FPGA Region
 * Caller will need to put_device(&region->dev) when done.
 * Returns FPGA Region struct or NULL
 */
static struct fpga_region *fpga_region_find(struct device_node *np)
{
	struct device *dev;

	dev = class_find_device(fpga_region_class, NULL, np,
				fpga_region_of_node_match);
	if (!dev)
		return NULL;

	return to_fpga_region(dev);
}

/**
 * fpga_region_get - get an exclusive reference to a fpga region
 * @region: FPGA Region struct
 *
 * Caller should call fpga_region_put() when done with region.
 *
 * Return fpga_region struct if successful.
 * Return -EBUSY if someone already has a reference to the region.
 * Return -ENODEV if @np is not a FPGA Region.
 */
static struct fpga_region *fpga_region_get(struct fpga_region *region)
{
	struct device *dev = &region->dev;

	if (!mutex_trylock(&region->mutex)) {
		dev_dbg(dev, "%s: FPGA Region already in use\n", __func__);
		return ERR_PTR(-EBUSY);
	}

	get_device(dev);
	of_node_get(dev->of_node);
	if (!try_module_get(dev->parent->driver->owner)) {
		of_node_put(dev->of_node);
		put_device(dev);
		mutex_unlock(&region->mutex);
		return ERR_PTR(-ENODEV);
	}

	dev_dbg(&region->dev, "get\n");

	return region;
}

/**
 * fpga_region_put - release a reference to a region
 *
 * @region: FPGA region
 */
static void fpga_region_put(struct fpga_region *region)
{
	struct device *dev = &region->dev;

	dev_dbg(&region->dev, "put\n");

	module_put(dev->parent->driver->owner);
	of_node_put(dev->of_node);
	put_device(dev);
	mutex_unlock(&region->mutex);
}

/**
 * fpga_region_get_manager - get exclusive reference for FPGA manager
 * @region: FPGA region
 *
 * Get FPGA Manager from "fpga-mgr" property or from ancestor region.
 *
 * Caller should call fpga_mgr_put() when done with manager.
 *
 * Return: fpga manager struct or IS_ERR() condition containing error code.
 */
static struct fpga_manager *fpga_region_get_manager(struct fpga_region *region)
{
	struct device *dev = &region->dev;
	struct device_node *np = dev->of_node;
	struct device_node  *mgr_node;
	struct fpga_manager *mgr;

	of_node_get(np);
	while (np) {
		if (of_device_is_compatible(np, "fpga-region")) {
			mgr_node = of_parse_phandle(np, "fpga-mgr", 0);
			if (mgr_node) {
				mgr = of_fpga_mgr_get(mgr_node);
				of_node_put(np);
				return mgr;
			}
		}
		np = of_get_next_parent(np);
	}
	of_node_put(np);

	return ERR_PTR(-EINVAL);
}

/**
 * fpga_region_get_bridges - create a list of bridges
 * @region: FPGA region
 * @overlay: device node of the overlay
 *
 * Create a list of bridges including the parent bridge and the bridges
 * specified by "fpga-bridges" property.  Note that the
 * fpga_bridges_enable/disable/put functions are all fine with an empty list
 * if that happens.
 *
 * Caller should call fpga_bridges_put(&region->bridge_list) when
 * done with the bridges.
 *
 * Return 0 for success (even if there are no bridges specified)
 * or -EBUSY if any of the bridges are in use.
 */
static int fpga_region_get_bridges(struct fpga_region *region,
				   struct device_node *overlay)
{
	struct device *dev = &region->dev;
	struct device_node *region_np = dev->of_node;
	struct device_node *br, *np, *parent_br = NULL;
	int i, ret;

	/* If parent is a bridge, add to list */
	ret = of_fpga_bridge_get_to_list(region_np->parent, region->info,
					 &region->bridge_list);

	/* -EBUSY means parent is a bridge that is under use. Give up. */
	if (ret == -EBUSY)
		return ret;

	/* Zero return code means parent was a bridge and was added to list. */
	if (!ret)
		parent_br = region_np->parent;

	/* If overlay has a list of bridges, use it. */
	if (of_parse_phandle(overlay, "fpga-bridges", 0))
		np = overlay;
	else
		np = region_np;

	for (i = 0; ; i++) {
		br = of_parse_phandle(np, "fpga-bridges", i);
		if (!br)
			break;

		/* If parent bridge is in list, skip it. */
		if (br == parent_br)
			continue;

		/* If node is a bridge, get it and add to list */
		ret = of_fpga_bridge_get_to_list(br, region->info,
						 &region->bridge_list);

		/* If any of the bridges are in use, give up */
		if (ret == -EBUSY) {
			fpga_bridges_put(&region->bridge_list);
			return -EBUSY;
		}
	}

	return 0;
}

/**
 * fpga_region_program_fpga - program FPGA
 * @region: FPGA region
 * @firmware_name: name of FPGA image firmware file
 * @overlay: device node of the overlay
 * Program an FPGA using information in the device tree.
 * Function assumes that there is a firmware-name property.
 * Return 0 for success or negative error code.
 */
static int fpga_region_program_fpga(struct fpga_region *region,
				    const char *firmware_name,
				    struct device_node *overlay)
{
	struct fpga_manager *mgr;
	int ret;

	region = fpga_region_get(region);
	if (IS_ERR(region)) {
		pr_err("failed to get fpga region\n");
		return PTR_ERR(region);
	}

	mgr = fpga_region_get_manager(region);
	if (IS_ERR(mgr)) {
		pr_err("failed to get fpga region manager\n");
		ret = PTR_ERR(mgr);
		goto err_put_region;
	}

	ret = fpga_region_get_bridges(region, overlay);
	if (ret) {
		pr_err("failed to get fpga region bridges\n");
		goto err_put_mgr;
	}

	ret = fpga_bridges_disable(&region->bridge_list);
	if (ret) {
		pr_err("failed to disable region bridges\n");
		goto err_put_br;
	}

	ret = fpga_mgr_firmware_load(mgr, region->info, firmware_name);
	if (ret) {
		pr_err("failed to load fpga image\n");
		goto err_put_br;
	}

	ret = fpga_bridges_enable(&region->bridge_list);
	if (ret) {
		pr_err("failed to enable region bridges\n");
		goto err_put_br;
	}

	fpga_mgr_put(mgr);
	fpga_region_put(region);

	return 0;

err_put_br:
	fpga_bridges_put(&region->bridge_list);
err_put_mgr:
	fpga_mgr_put(mgr);
err_put_region:
	fpga_region_put(region);

	return ret;
}

/**
 * child_regions_with_firmware
 * @overlay: device node of the overlay
 *
 * If the overlay adds child FPGA regions, they are not allowed to have
 * firmware-name property.
 *
 * Return 0 for OK or -EINVAL if child FPGA region adds firmware-name.
 */
static int child_regions_with_firmware(struct device_node *overlay)
{
	struct device_node *child_region;
	const char *child_firmware_name;
	int ret = 0;

	of_node_get(overlay);

	child_region = of_find_matching_node(overlay, fpga_region_of_match);
	while (child_region) {
		if (!of_property_read_string(child_region, "firmware-name",
					     &child_firmware_name)) {
			ret = -EINVAL;
			break;
		}
		child_region = of_find_matching_node(child_region,
						     fpga_region_of_match);
	}

	of_node_put(child_region);

	if (ret)
		pr_err("firmware-name not allowed in child FPGA region: %pOF",
		       child_region);

	return ret;
}

/**
 * fpga_region_notify_pre_apply - pre-apply overlay notification
 *
 * @region: FPGA region that the overlay was applied to
 * @nd: overlay notification data
 *
 * Called after when an overlay targeted to a FPGA Region is about to be
 * applied.  Function will check the properties that will be added to the FPGA
 * region.  If the checks pass, it will program the FPGA.
 *
 * The checks are:
 * The overlay must add either firmware-name or external-fpga-config property
 * to the FPGA Region.
 *
 *   firmware-name         : program the FPGA
 *   external-fpga-config  : FPGA is already programmed
 *   encrypted-fpga-config : FPGA bitstream is encrypted
 *
 * The overlay can add other FPGA regions, but child FPGA regions cannot have a
 * firmware-name property since those regions don't exist yet.
 *
 * If the overlay that breaks the rules, notifier returns an error and the
 * overlay is rejected before it goes into the main tree.
 *
 * Returns 0 for success or negative error code for failure.
 */
static int fpga_region_notify_pre_apply(struct fpga_region *region,
					struct of_overlay_notify_data *nd)
{
	const char *firmware_name = NULL;
	struct fpga_image_info *info;
	int ret;

	info = devm_kzalloc(&region->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	region->info = info;

	/* Reject overlay if child FPGA Regions have firmware-name property */
	ret = child_regions_with_firmware(nd->overlay);
	if (ret)
		return ret;

	/* Read FPGA region properties from the overlay */
	if (of_property_read_bool(nd->overlay, "partial-fpga-config"))
		info->flags |= FPGA_MGR_PARTIAL_RECONFIG;

	if (of_property_read_bool(nd->overlay, "external-fpga-config"))
		info->flags |= FPGA_MGR_EXTERNAL_CONFIG;

	if (of_property_read_bool(nd->overlay, "encrypted-fpga-config"))
		info->flags |= FPGA_MGR_ENCRYPTED_BITSTREAM;

	of_property_read_string(nd->overlay, "firmware-name", &firmware_name);

	of_property_read_u32(nd->overlay, "region-unfreeze-timeout-us",
			     &info->enable_timeout_us);

	of_property_read_u32(nd->overlay, "region-freeze-timeout-us",
			     &info->disable_timeout_us);

	of_property_read_u32(nd->overlay, "config-complete-timeout-us",
			     &info->config_complete_timeout_us);

	/* If FPGA was externally programmed, don't specify firmware */
	if ((info->flags & FPGA_MGR_EXTERNAL_CONFIG) && firmware_name) {
		pr_err("error: specified firmware and external-fpga-config");
		return -EINVAL;
	}

	/* FPGA is already configured externally.  We're done. */
	if (info->flags & FPGA_MGR_EXTERNAL_CONFIG)
		return 0;

	/* If we got this far, we should be programming the FPGA */
	if (!firmware_name) {
		pr_err("should specify firmware-name or external-fpga-config\n");
		return -EINVAL;
	}

	return fpga_region_program_fpga(region, firmware_name, nd->overlay);
}

/**
 * fpga_region_notify_post_remove - post-remove overlay notification
 *
 * @region: FPGA region that was targeted by the overlay that was removed
 * @nd: overlay notification data
 *
 * Called after an overlay has been removed if the overlay's target was a
 * FPGA region.
 */
static void fpga_region_notify_post_remove(struct fpga_region *region,
					   struct of_overlay_notify_data *nd)
{
	fpga_bridges_disable(&region->bridge_list);
	fpga_bridges_put(&region->bridge_list);
	devm_kfree(&region->dev, region->info);
	region->info = NULL;
}

/**
 * of_fpga_region_notify - reconfig notifier for dynamic DT changes
 * @nb:		notifier block
 * @action:	notifier action
 * @arg:	reconfig data
 *
 * This notifier handles programming a FPGA when a "firmware-name" property is
 * added to a fpga-region.
 *
 * Returns NOTIFY_OK or error if FPGA programming fails.
 */
static int of_fpga_region_notify(struct notifier_block *nb,
				 unsigned long action, void *arg)
{
	struct of_overlay_notify_data *nd = arg;
	struct fpga_region *region;
	int ret;

	switch (action) {
	case OF_OVERLAY_PRE_APPLY:
		pr_debug("%s OF_OVERLAY_PRE_APPLY\n", __func__);
		break;
	case OF_OVERLAY_POST_APPLY:
		pr_debug("%s OF_OVERLAY_POST_APPLY\n", __func__);
		return NOTIFY_OK;       /* not for us */
	case OF_OVERLAY_PRE_REMOVE:
		pr_debug("%s OF_OVERLAY_PRE_REMOVE\n", __func__);
		return NOTIFY_OK;       /* not for us */
	case OF_OVERLAY_POST_REMOVE:
		pr_debug("%s OF_OVERLAY_POST_REMOVE\n", __func__);
		break;
	default:			/* should not happen */
		return NOTIFY_OK;
	}

	region = fpga_region_find(nd->target);
	if (!region)
		return NOTIFY_OK;

	ret = 0;
	switch (action) {
	case OF_OVERLAY_PRE_APPLY:
		ret = fpga_region_notify_pre_apply(region, nd);
		break;

	case OF_OVERLAY_POST_REMOVE:
		fpga_region_notify_post_remove(region, nd);
		break;
	}

	put_device(&region->dev);

	if (ret)
		return notifier_from_errno(ret);

	return NOTIFY_OK;
}

static struct notifier_block fpga_region_of_nb = {
	.notifier_call = of_fpga_region_notify,
};

static int fpga_region_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct fpga_region *region;
	int id, ret = 0;

	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return -ENOMEM;

	id = ida_simple_get(&fpga_region_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		ret = id;
		goto err_kfree;
	}

	mutex_init(&region->mutex);
	INIT_LIST_HEAD(&region->bridge_list);

	device_initialize(&region->dev);
	region->dev.class = fpga_region_class;
	region->dev.parent = dev;
	region->dev.of_node = np;
	region->dev.id = id;
	dev_set_drvdata(dev, region);

	ret = dev_set_name(&region->dev, "region%d", id);
	if (ret)
		goto err_remove;

	ret = device_add(&region->dev);
	if (ret)
		goto err_remove;

	of_platform_populate(np, fpga_region_of_match, NULL, &region->dev);

	dev_info(dev, "FPGA Region probed\n");

	return 0;

err_remove:
	ida_simple_remove(&fpga_region_ida, id);
err_kfree:
	kfree(region);

	return ret;
}

static int fpga_region_remove(struct platform_device *pdev)
{
	struct fpga_region *region = platform_get_drvdata(pdev);

	device_unregister(&region->dev);

	return 0;
}

static struct platform_driver fpga_region_driver = {
	.probe = fpga_region_probe,
	.remove = fpga_region_remove,
	.driver = {
		.name	= "fpga-region",
		.of_match_table = of_match_ptr(fpga_region_of_match),
	},
};

static void fpga_region_dev_release(struct device *dev)
{
	struct fpga_region *region = to_fpga_region(dev);

	ida_simple_remove(&fpga_region_ida, region->dev.id);
	kfree(region);
}

/**
 * fpga_region_init - init function for fpga_region class
 * Creates the fpga_region class and registers a reconfig notifier.
 */
static int __init fpga_region_init(void)
{
	int ret;

	fpga_region_class = class_create(THIS_MODULE, "fpga_region");
	if (IS_ERR(fpga_region_class))
		return PTR_ERR(fpga_region_class);

	fpga_region_class->dev_release = fpga_region_dev_release;

	ret = of_overlay_notifier_register(&fpga_region_of_nb);
	if (ret)
		goto err_class;

	ret = platform_driver_register(&fpga_region_driver);
	if (ret)
		goto err_plat;

	return 0;

err_plat:
	of_overlay_notifier_unregister(&fpga_region_of_nb);
err_class:
	class_destroy(fpga_region_class);
	ida_destroy(&fpga_region_ida);
	return ret;
}

static void __exit fpga_region_exit(void)
{
	platform_driver_unregister(&fpga_region_driver);
	of_overlay_notifier_unregister(&fpga_region_of_nb);
	class_destroy(fpga_region_class);
	ida_destroy(&fpga_region_ida);
}

subsys_initcall(fpga_region_init);
module_exit(fpga_region_exit);

MODULE_DESCRIPTION("FPGA Region");
MODULE_AUTHOR("Alan Tull <atull@opensource.altera.com>");
MODULE_LICENSE("GPL v2");
