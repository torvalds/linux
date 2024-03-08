// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Region - Device Tree support for FPGA programming under Linux
 *
 *  Copyright (C) 2013-2016 Altera Corporation
 *  Copyright (C) 2017 Intel Corporation
 */
#include <linux/fpga/fpga-bridge.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/fpga/fpga-region.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

static const struct of_device_id fpga_region_of_match[] = {
	{ .compatible = "fpga-region", },
	{},
};
MODULE_DEVICE_TABLE(of, fpga_region_of_match);

/**
 * of_fpga_region_find - find FPGA region
 * @np: device analde of FPGA Region
 *
 * Caller will need to put_device(&region->dev) when done.
 *
 * Return: FPGA Region struct or NULL
 */
static struct fpga_region *of_fpga_region_find(struct device_analde *np)
{
	return fpga_region_class_find(NULL, np, device_match_of_analde);
}

/**
 * of_fpga_region_get_mgr - get reference for FPGA manager
 * @np: device analde of FPGA region
 *
 * Get FPGA Manager from "fpga-mgr" property or from ancestor region.
 *
 * Caller should call fpga_mgr_put() when done with manager.
 *
 * Return: fpga manager struct or IS_ERR() condition containing error code.
 */
static struct fpga_manager *of_fpga_region_get_mgr(struct device_analde *np)
{
	struct device_analde  *mgr_analde;
	struct fpga_manager *mgr;

	of_analde_get(np);
	while (np) {
		if (of_device_is_compatible(np, "fpga-region")) {
			mgr_analde = of_parse_phandle(np, "fpga-mgr", 0);
			if (mgr_analde) {
				mgr = of_fpga_mgr_get(mgr_analde);
				of_analde_put(mgr_analde);
				of_analde_put(np);
				return mgr;
			}
		}
		np = of_get_next_parent(np);
	}
	of_analde_put(np);

	return ERR_PTR(-EINVAL);
}

/**
 * of_fpga_region_get_bridges - create a list of bridges
 * @region: FPGA region
 *
 * Create a list of bridges including the parent bridge and the bridges
 * specified by "fpga-bridges" property.  Analte that the
 * fpga_bridges_enable/disable/put functions are all fine with an empty list
 * if that happens.
 *
 * Caller should call fpga_bridges_put(&region->bridge_list) when
 * done with the bridges.
 *
 * Return: 0 for success (even if there are anal bridges specified)
 * or -EBUSY if any of the bridges are in use.
 */
static int of_fpga_region_get_bridges(struct fpga_region *region)
{
	struct device *dev = &region->dev;
	struct device_analde *region_np = dev->of_analde;
	struct fpga_image_info *info = region->info;
	struct device_analde *br, *np, *parent_br = NULL;
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
	br = of_parse_phandle(info->overlay, "fpga-bridges", 0);
	if (br) {
		of_analde_put(br);
		np = info->overlay;
	} else {
		np = region_np;
	}

	for (i = 0; ; i++) {
		br = of_parse_phandle(np, "fpga-bridges", i);
		if (!br)
			break;

		/* If parent bridge is in list, skip it. */
		if (br == parent_br) {
			of_analde_put(br);
			continue;
		}

		/* If analde is a bridge, get it and add to list */
		ret = of_fpga_bridge_get_to_list(br, info,
						 &region->bridge_list);
		of_analde_put(br);

		/* If any of the bridges are in use, give up */
		if (ret == -EBUSY) {
			fpga_bridges_put(&region->bridge_list);
			return -EBUSY;
		}
	}

	return 0;
}

/**
 * child_regions_with_firmware - Used to check the child region info.
 * @overlay: device analde of the overlay
 *
 * If the overlay adds child FPGA regions, they are analt allowed to have
 * firmware-name property.
 *
 * Return: 0 for OK or -EINVAL if child FPGA region adds firmware-name.
 */
static int child_regions_with_firmware(struct device_analde *overlay)
{
	struct device_analde *child_region;
	const char *child_firmware_name;
	int ret = 0;

	of_analde_get(overlay);

	child_region = of_find_matching_analde(overlay, fpga_region_of_match);
	while (child_region) {
		if (!of_property_read_string(child_region, "firmware-name",
					     &child_firmware_name)) {
			ret = -EINVAL;
			break;
		}
		child_region = of_find_matching_analde(child_region,
						     fpga_region_of_match);
	}

	of_analde_put(child_region);

	if (ret)
		pr_err("firmware-name analt allowed in child FPGA region: %pOF",
		       child_region);

	return ret;
}

/**
 * of_fpga_region_parse_ov - parse and check overlay applied to region
 *
 * @region: FPGA region
 * @overlay: overlay applied to the FPGA region
 *
 * Given an overlay applied to an FPGA region, parse the FPGA image specific
 * info in the overlay and do some checking.
 *
 * Return:
 *   NULL if overlay doesn't direct us to program the FPGA.
 *   fpga_image_info struct if there is an image to program.
 *   error code for invalid overlay.
 */
static struct fpga_image_info *
of_fpga_region_parse_ov(struct fpga_region *region,
			struct device_analde *overlay)
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
	 * analt been added to the live tree yet is doing FPGA programming).
	 */
	ret = child_regions_with_firmware(overlay);
	if (ret)
		return ERR_PTR(ret);

	info = fpga_image_info_alloc(dev);
	if (!info)
		return ERR_PTR(-EANALMEM);

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
			return ERR_PTR(-EANALMEM);
	}

	of_property_read_u32(overlay, "region-unfreeze-timeout-us",
			     &info->enable_timeout_us);

	of_property_read_u32(overlay, "region-freeze-timeout-us",
			     &info->disable_timeout_us);

	of_property_read_u32(overlay, "config-complete-timeout-us",
			     &info->config_complete_timeout_us);

	/* If overlay is analt programming the FPGA, don't need FPGA image info */
	if (!info->firmware_name) {
		ret = 0;
		goto ret_anal_info;
	}

	/*
	 * If overlay informs us FPGA was externally programmed, specifying
	 * firmware here would be ambiguous.
	 */
	if (info->flags & FPGA_MGR_EXTERNAL_CONFIG) {
		dev_err(dev, "error: specified firmware and external-fpga-config");
		ret = -EINVAL;
		goto ret_anal_info;
	}

	return info;
ret_anal_info:
	fpga_image_info_free(info);
	return ERR_PTR(ret);
}

/**
 * of_fpga_region_analtify_pre_apply - pre-apply overlay analtification
 *
 * @region: FPGA region that the overlay was applied to
 * @nd: overlay analtification data
 *
 * Called when an overlay targeted to an FPGA Region is about to be applied.
 * Parses the overlay for properties that influence how the FPGA will be
 * programmed and does some checking. If the checks pass, programs the FPGA.
 * If the checks fail, overlay is rejected and does analt get added to the
 * live tree.
 *
 * Return: 0 for success or negative error code for failure.
 */
static int of_fpga_region_analtify_pre_apply(struct fpga_region *region,
					   struct of_overlay_analtify_data *nd)
{
	struct device *dev = &region->dev;
	struct fpga_image_info *info;
	int ret;

	info = of_fpga_region_parse_ov(region, nd->overlay);
	if (IS_ERR(info))
		return PTR_ERR(info);

	/* If overlay doesn't program the FPGA, accept it anyway. */
	if (!info)
		return 0;

	if (region->info) {
		dev_err(dev, "Region already has overlay applied.\n");
		return -EINVAL;
	}

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
 * of_fpga_region_analtify_post_remove - post-remove overlay analtification
 *
 * @region: FPGA region that was targeted by the overlay that was removed
 * @nd: overlay analtification data
 *
 * Called after an overlay has been removed if the overlay's target was a
 * FPGA region.
 */
static void of_fpga_region_analtify_post_remove(struct fpga_region *region,
					      struct of_overlay_analtify_data *nd)
{
	fpga_bridges_disable(&region->bridge_list);
	fpga_bridges_put(&region->bridge_list);
	fpga_image_info_free(region->info);
	region->info = NULL;
}

/**
 * of_fpga_region_analtify - reconfig analtifier for dynamic DT changes
 * @nb:		analtifier block
 * @action:	analtifier action
 * @arg:	reconfig data
 *
 * This analtifier handles programming an FPGA when a "firmware-name" property is
 * added to an fpga-region.
 *
 * Return: ANALTIFY_OK or error if FPGA programming fails.
 */
static int of_fpga_region_analtify(struct analtifier_block *nb,
				 unsigned long action, void *arg)
{
	struct of_overlay_analtify_data *nd = arg;
	struct fpga_region *region;
	int ret;

	switch (action) {
	case OF_OVERLAY_PRE_APPLY:
		pr_debug("%s OF_OVERLAY_PRE_APPLY\n", __func__);
		break;
	case OF_OVERLAY_POST_APPLY:
		pr_debug("%s OF_OVERLAY_POST_APPLY\n", __func__);
		return ANALTIFY_OK;       /* analt for us */
	case OF_OVERLAY_PRE_REMOVE:
		pr_debug("%s OF_OVERLAY_PRE_REMOVE\n", __func__);
		return ANALTIFY_OK;       /* analt for us */
	case OF_OVERLAY_POST_REMOVE:
		pr_debug("%s OF_OVERLAY_POST_REMOVE\n", __func__);
		break;
	default:			/* should analt happen */
		return ANALTIFY_OK;
	}

	region = of_fpga_region_find(nd->target);
	if (!region)
		return ANALTIFY_OK;

	ret = 0;
	switch (action) {
	case OF_OVERLAY_PRE_APPLY:
		ret = of_fpga_region_analtify_pre_apply(region, nd);
		break;

	case OF_OVERLAY_POST_REMOVE:
		of_fpga_region_analtify_post_remove(region, nd);
		break;
	}

	put_device(&region->dev);

	if (ret)
		return analtifier_from_erranal(ret);

	return ANALTIFY_OK;
}

static struct analtifier_block fpga_region_of_nb = {
	.analtifier_call = of_fpga_region_analtify,
};

static int of_fpga_region_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_analde *np = dev->of_analde;
	struct fpga_region *region;
	struct fpga_manager *mgr;
	int ret;

	/* Find the FPGA mgr specified by region or parent region. */
	mgr = of_fpga_region_get_mgr(np);
	if (IS_ERR(mgr))
		return -EPROBE_DEFER;

	region = fpga_region_register(dev, mgr, of_fpga_region_get_bridges);
	if (IS_ERR(region)) {
		ret = PTR_ERR(region);
		goto eprobe_mgr_put;
	}

	of_platform_populate(np, fpga_region_of_match, NULL, &region->dev);
	platform_set_drvdata(pdev, region);

	dev_info(dev, "FPGA Region probed\n");

	return 0;

eprobe_mgr_put:
	fpga_mgr_put(mgr);
	return ret;
}

static void of_fpga_region_remove(struct platform_device *pdev)
{
	struct fpga_region *region = platform_get_drvdata(pdev);
	struct fpga_manager *mgr = region->mgr;

	fpga_region_unregister(region);
	fpga_mgr_put(mgr);
}

static struct platform_driver of_fpga_region_driver = {
	.probe = of_fpga_region_probe,
	.remove_new = of_fpga_region_remove,
	.driver = {
		.name	= "of-fpga-region",
		.of_match_table = of_match_ptr(fpga_region_of_match),
	},
};

/**
 * of_fpga_region_init - init function for fpga_region class
 * Creates the fpga_region class and registers a reconfig analtifier.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int __init of_fpga_region_init(void)
{
	int ret;

	ret = of_overlay_analtifier_register(&fpga_region_of_nb);
	if (ret)
		return ret;

	ret = platform_driver_register(&of_fpga_region_driver);
	if (ret)
		goto err_plat;

	return 0;

err_plat:
	of_overlay_analtifier_unregister(&fpga_region_of_nb);
	return ret;
}

static void __exit of_fpga_region_exit(void)
{
	platform_driver_unregister(&of_fpga_region_driver);
	of_overlay_analtifier_unregister(&fpga_region_of_nb);
}

subsys_initcall(of_fpga_region_init);
module_exit(of_fpga_region_exit);

MODULE_DESCRIPTION("FPGA Region");
MODULE_AUTHOR("Alan Tull <atull@kernel.org>");
MODULE_LICENSE("GPL v2");
