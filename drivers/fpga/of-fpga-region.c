/*
 * FPGA Region - Device Tree support for FPGA programming under Linux
 *
 *  Copyright (C) 2013-2016 Altera Corporation
 *  Copyright (C) 2017 Intel Corporation
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
#include <linux/fpga/fpga-region.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

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
 * of_fpga_region_find - find FPGA region
 * @np: device node of FPGA Region
 *
 * Caller will need to put_device(&region->dev) when done.
 *
 * Returns FPGA Region struct or NULL
 */
static struct fpga_region *of_fpga_region_find(struct device_node *np)
{
	return fpga_region_class_find(NULL, np, fpga_region_of_node_match);
}

/**
 * of_fpga_region_get_mgr - get reference for FPGA manager
 * @np: device node of FPGA region
 *
 * Get FPGA Manager from "fpga-mgr" property or from ancestor region.
 *
 * Caller should call fpga_mgr_put() when done with manager.
 *
 * Return: fpga manager struct or IS_ERR() condition containing error code.
 */
static struct fpga_manager *of_fpga_region_get_mgr(struct device_node *np)
{
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
 * of_fpga_region_get_bridges - create a list of bridges
 * @region: FPGA region
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
static int of_fpga_region_get_bridges(struct fpga_region *region)
{
	struct device *dev = &region->dev;
	struct device_node *region_np = dev->of_node;
	struct fpga_image_info *info = region->info;
	struct device_node *br, *np, *parent_br = NULL;
	int i, ret;

	/* If parent is a bridge, add to list */
	ret = of_fpga_bridge_get_to_list(region_np->parent, info,
					 &region->bridge_list);

	/* -EBUSY means parent is a bridge that is under use. Give up. */
	if (ret == -EBUSY)
		return ret;

	/* Zero return code means parent was a bridge and was added to list. */
	if (!ret)
		parent_br = region_np->parent;

	/* If overlay has a list of bridges, use it. */
	if (of_parse_phandle(info->overlay, "fpga-bridges", 0))
		np = info->overlay;
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
		ret = of_fpga_bridge_get_to_list(br, info,
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
 * of_fpga_region_parse_ov - parse and check overlay applied to region
 *
 * @region: FPGA region
 * @overlay: overlay applied to the FPGA region
 *
 * Given an overlay applied to a FPGA region, parse the FPGA image specific
 * info in the overlay and do some checking.
 *
 * Returns:
 *   NULL if overlay doesn't direct us to program the FPGA.
 *   fpga_image_info struct if there is an image to program.
 *   error code for invalid overlay.
 */
static struct fpga_image_info *of_fpga_region_parse_ov(
						struct fpga_region *region,
						struct device_node *overlay)
{
	struct device *dev = &region->dev;
	struct fpga_image_info *info;
	const char *firmware_name;
	int ret;

	if (region->info) {
		dev_err(dev, "Region already has overlay applied.\n");
		return ERR_PTR(-EINVAL);
	}

	/*
	 * Reject overlay if child FPGA Regions added in the overlay have
	 * firmware-name property (would mean that an FPGA region that has
	 * not been added to the live tree yet is doing FPGA programming).
	 */
	ret = child_regions_with_firmware(overlay);
	if (ret)
		return ERR_PTR(ret);

	info = fpga_image_info_alloc(dev);
	if (!info)
		return ERR_PTR(-ENOMEM);

	info->overlay = overlay;

	/* Read FPGA region properties from the overlay */
	if (of_property_read_bool(overlay, "partial-fpga-config"))
		info->flags |= FPGA_MGR_PARTIAL_RECONFIG;

	if (of_property_read_bool(overlay, "external-fpga-config"))
		info->flags |= FPGA_MGR_EXTERNAL_CONFIG;

	if (of_property_read_bool(overlay, "encrypted-fpga-config"))
		info->flags |= FPGA_MGR_ENCRYPTED_BITSTREAM;

	if (!of_property_read_string(overlay, "firmware-name",
				     &firmware_name)) {
		info->firmware_name = devm_kstrdup(dev, firmware_name,
						   GFP_KERNEL);
		if (!info->firmware_name)
			return ERR_PTR(-ENOMEM);
	}

	of_property_read_u32(overlay, "region-unfreeze-timeout-us",
			     &info->enable_timeout_us);

	of_property_read_u32(overlay, "region-freeze-timeout-us",
			     &info->disable_timeout_us);

	of_property_read_u32(overlay, "config-complete-timeout-us",
			     &info->config_complete_timeout_us);

	/* If overlay is not programming the FPGA, don't need FPGA image info */
	if (!info->firmware_name) {
		ret = 0;
		goto ret_no_info;
	}

	/*
	 * If overlay informs us FPGA was externally programmed, specifying
	 * firmware here would be ambiguous.
	 */
	if (info->flags & FPGA_MGR_EXTERNAL_CONFIG) {
		dev_err(dev, "error: specified firmware and external-fpga-config");
		ret = -EINVAL;
		goto ret_no_info;
	}

	return info;
ret_no_info:
	fpga_image_info_free(info);
	return ERR_PTR(ret);
}

/**
 * of_fpga_region_notify_pre_apply - pre-apply overlay notification
 *
 * @region: FPGA region that the overlay was applied to
 * @nd: overlay notification data
 *
 * Called when an overlay targeted to a FPGA Region is about to be applied.
 * Parses the overlay for properties that influence how the FPGA will be
 * programmed and does some checking. If the checks pass, programs the FPGA.
 * If the checks fail, overlay is rejected and does not get added to the
 * live tree.
 *
 * Returns 0 for success or negative error code for failure.
 */
static int of_fpga_region_notify_pre_apply(struct fpga_region *region,
					   struct of_overlay_notify_data *nd)
{
	struct device *dev = &region->dev;
	struct fpga_image_info *info;
	int ret;

	if (region->info) {
		dev_err(dev, "Region already has overlay applied.\n");
		return -EINVAL;
	}

	info = of_fpga_region_parse_ov(region, nd->overlay);
	if (IS_ERR(info))
		return PTR_ERR(info);

	if (!info)
		return 0;

	region->info = info;
	ret = fpga_region_program_fpga(region);
	if (ret) {
		/* error; reject overlay */
		fpga_image_info_free(info);
		region->info = NULL;
	}

	return ret;
}

/**
 * of_fpga_region_notify_post_remove - post-remove overlay notification
 *
 * @region: FPGA region that was targeted by the overlay that was removed
 * @nd: overlay notification data
 *
 * Called after an overlay has been removed if the overlay's target was a
 * FPGA region.
 */
static void of_fpga_region_notify_post_remove(struct fpga_region *region,
					      struct of_overlay_notify_data *nd)
{
	fpga_bridges_disable(&region->bridge_list);
	fpga_bridges_put(&region->bridge_list);
	fpga_image_info_free(region->info);
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

	region = of_fpga_region_find(nd->target);
	if (!region)
		return NOTIFY_OK;

	ret = 0;
	switch (action) {
	case OF_OVERLAY_PRE_APPLY:
		ret = of_fpga_region_notify_pre_apply(region, nd);
		break;

	case OF_OVERLAY_POST_REMOVE:
		of_fpga_region_notify_post_remove(region, nd);
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

static int of_fpga_region_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct fpga_region *region;
	struct fpga_manager *mgr;
	int ret;

	/* Find the FPGA mgr specified by region or parent region. */
	mgr = of_fpga_region_get_mgr(np);
	if (IS_ERR(mgr))
		return -EPROBE_DEFER;

	region = devm_kzalloc(dev, sizeof(*region), GFP_KERNEL);
	if (!region) {
		ret = -ENOMEM;
		goto eprobe_mgr_put;
	}

	region->mgr = mgr;

	/* Specify how to get bridges for this type of region. */
	region->get_bridges = of_fpga_region_get_bridges;

	ret = fpga_region_register(dev, region);
	if (ret)
		goto eprobe_mgr_put;

	of_platform_populate(np, fpga_region_of_match, NULL, &region->dev);

	dev_info(dev, "FPGA Region probed\n");

	return 0;

eprobe_mgr_put:
	fpga_mgr_put(mgr);
	return ret;
}

static int of_fpga_region_remove(struct platform_device *pdev)
{
	struct fpga_region *region = platform_get_drvdata(pdev);

	fpga_region_unregister(region);
	fpga_mgr_put(region->mgr);

	return 0;
}

static struct platform_driver of_fpga_region_driver = {
	.probe = of_fpga_region_probe,
	.remove = of_fpga_region_remove,
	.driver = {
		.name	= "of-fpga-region",
		.of_match_table = of_match_ptr(fpga_region_of_match),
	},
};

/**
 * fpga_region_init - init function for fpga_region class
 * Creates the fpga_region class and registers a reconfig notifier.
 */
static int __init of_fpga_region_init(void)
{
	int ret;

	ret = of_overlay_notifier_register(&fpga_region_of_nb);
	if (ret)
		return ret;

	ret = platform_driver_register(&of_fpga_region_driver);
	if (ret)
		goto err_plat;

	return 0;

err_plat:
	of_overlay_notifier_unregister(&fpga_region_of_nb);
	return ret;
}

static void __exit of_fpga_region_exit(void)
{
	platform_driver_unregister(&of_fpga_region_driver);
	of_overlay_notifier_unregister(&fpga_region_of_nb);
}

subsys_initcall(of_fpga_region_init);
module_exit(of_fpga_region_exit);

MODULE_DESCRIPTION("FPGA Region");
MODULE_AUTHOR("Alan Tull <atull@kernel.org>");
MODULE_LICENSE("GPL v2");
