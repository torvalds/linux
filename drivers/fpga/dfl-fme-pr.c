// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for FPGA Management Engine (FME) Partial Reconfiguration
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *   Kang Luwei <luwei.kang@intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *   Wu Hao <hao.wu@intel.com>
 *   Joseph Grecco <joe.grecco@intel.com>
 *   Enno Luebbers <enno.luebbers@intel.com>
 *   Tim Whisonant <tim.whisonant@intel.com>
 *   Ananda Ravuri <ananda.ravuri@intel.com>
 *   Christopher Rauer <christopher.rauer@intel.com>
 *   Henry Mitchel <henry.mitchel@intel.com>
 */

#include <linux/types.h>
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/fpga/fpga-bridge.h>
#include <linux/fpga/fpga-region.h>
#include <linux/fpga-dfl.h>

#include "dfl.h"
#include "dfl-fme.h"
#include "dfl-fme-pr.h"

static struct dfl_fme_region *
dfl_fme_region_find_by_port_id(struct dfl_fme *fme, int port_id)
{
	struct dfl_fme_region *fme_region;

	list_for_each_entry(fme_region, &fme->region_list, node)
		if (fme_region->port_id == port_id)
			return fme_region;

	return NULL;
}

static int dfl_fme_region_match(struct device *dev, const void *data)
{
	return dev->parent == data;
}

static struct fpga_region *dfl_fme_region_find(struct dfl_fme *fme, int port_id)
{
	struct dfl_fme_region *fme_region;
	struct fpga_region *region;

	fme_region = dfl_fme_region_find_by_port_id(fme, port_id);
	if (!fme_region)
		return NULL;

	region = fpga_region_class_find(NULL, &fme_region->region->dev,
					dfl_fme_region_match);
	if (!region)
		return NULL;

	return region;
}

static int fme_pr(struct platform_device *pdev, unsigned long arg)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	void __user *argp = (void __user *)arg;
	struct dfl_fpga_fme_port_pr port_pr;
	struct fpga_image_info *info;
	struct fpga_region *region;
	void __iomem *fme_hdr;
	struct dfl_fme *fme;
	unsigned long minsz;
	void *buf = NULL;
	size_t length;
	int ret = 0;
	u64 v;

	minsz = offsetofend(struct dfl_fpga_fme_port_pr, buffer_address);

	if (copy_from_user(&port_pr, argp, minsz))
		return -EFAULT;

	if (port_pr.argsz < minsz || port_pr.flags)
		return -EINVAL;

	/* get fme header region */
	fme_hdr = dfl_get_feature_ioaddr_by_id(&pdev->dev,
					       FME_FEATURE_ID_HEADER);

	/* check port id */
	v = readq(fme_hdr + FME_HDR_CAP);
	if (port_pr.port_id >= FIELD_GET(FME_CAP_NUM_PORTS, v)) {
		dev_dbg(&pdev->dev, "port number more than maximum\n");
		return -EINVAL;
	}

	/*
	 * align PR buffer per PR bandwidth, as HW ignores the extra padding
	 * data automatically.
	 */
	length = ALIGN(port_pr.buffer_size, 4);

	buf = vmalloc(length);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf,
			   (void __user *)(unsigned long)port_pr.buffer_address,
			   port_pr.buffer_size)) {
		ret = -EFAULT;
		goto free_exit;
	}

	/* prepare fpga_image_info for PR */
	info = fpga_image_info_alloc(&pdev->dev);
	if (!info) {
		ret = -ENOMEM;
		goto free_exit;
	}

	info->flags |= FPGA_MGR_PARTIAL_RECONFIG;

	mutex_lock(&pdata->lock);
	fme = dfl_fpga_pdata_get_private(pdata);
	/* fme device has been unregistered. */
	if (!fme) {
		ret = -EINVAL;
		goto unlock_exit;
	}

	region = dfl_fme_region_find(fme, port_pr.port_id);
	if (!region) {
		ret = -EINVAL;
		goto unlock_exit;
	}

	fpga_image_info_free(region->info);

	info->buf = buf;
	info->count = length;
	info->region_id = port_pr.port_id;
	region->info = info;

	ret = fpga_region_program_fpga(region);

	/*
	 * it allows userspace to reset the PR region's logic by disabling and
	 * reenabling the bridge to clear things out between acceleration runs.
	 * so no need to hold the bridges after partial reconfiguration.
	 */
	if (region->get_bridges)
		fpga_bridges_put(&region->bridge_list);

	put_device(&region->dev);
unlock_exit:
	mutex_unlock(&pdata->lock);
free_exit:
	vfree(buf);
	return ret;
}

/**
 * dfl_fme_create_mgr - create fpga mgr platform device as child device
 *
 * @pdata: fme platform_device's pdata
 *
 * Return: mgr platform device if successful, and error code otherwise.
 */
static struct platform_device *
dfl_fme_create_mgr(struct dfl_feature_platform_data *pdata,
		   struct dfl_feature *feature)
{
	struct platform_device *mgr, *fme = pdata->dev;
	struct dfl_fme_mgr_pdata mgr_pdata;
	int ret = -ENOMEM;

	if (!feature->ioaddr)
		return ERR_PTR(-ENODEV);

	mgr_pdata.ioaddr = feature->ioaddr;

	/*
	 * Each FME has only one fpga-mgr, so allocate platform device using
	 * the same FME platform device id.
	 */
	mgr = platform_device_alloc(DFL_FPGA_FME_MGR, fme->id);
	if (!mgr)
		return ERR_PTR(ret);

	mgr->dev.parent = &fme->dev;

	ret = platform_device_add_data(mgr, &mgr_pdata, sizeof(mgr_pdata));
	if (ret)
		goto create_mgr_err;

	ret = platform_device_add(mgr);
	if (ret)
		goto create_mgr_err;

	return mgr;

create_mgr_err:
	platform_device_put(mgr);
	return ERR_PTR(ret);
}

/**
 * dfl_fme_destroy_mgr - destroy fpga mgr platform device
 * @pdata: fme platform device's pdata
 */
static void dfl_fme_destroy_mgr(struct dfl_feature_platform_data *pdata)
{
	struct dfl_fme *priv = dfl_fpga_pdata_get_private(pdata);

	platform_device_unregister(priv->mgr);
}

/**
 * dfl_fme_create_bridge - create fme fpga bridge platform device as child
 *
 * @pdata: fme platform device's pdata
 * @port_id: port id for the bridge to be created.
 *
 * Return: bridge platform device if successful, and error code otherwise.
 */
static struct dfl_fme_bridge *
dfl_fme_create_bridge(struct dfl_feature_platform_data *pdata, int port_id)
{
	struct device *dev = &pdata->dev->dev;
	struct dfl_fme_br_pdata br_pdata;
	struct dfl_fme_bridge *fme_br;
	int ret = -ENOMEM;

	fme_br = devm_kzalloc(dev, sizeof(*fme_br), GFP_KERNEL);
	if (!fme_br)
		return ERR_PTR(ret);

	br_pdata.cdev = pdata->dfl_cdev;
	br_pdata.port_id = port_id;

	fme_br->br = platform_device_alloc(DFL_FPGA_FME_BRIDGE,
					   PLATFORM_DEVID_AUTO);
	if (!fme_br->br)
		return ERR_PTR(ret);

	fme_br->br->dev.parent = dev;

	ret = platform_device_add_data(fme_br->br, &br_pdata, sizeof(br_pdata));
	if (ret)
		goto create_br_err;

	ret = platform_device_add(fme_br->br);
	if (ret)
		goto create_br_err;

	return fme_br;

create_br_err:
	platform_device_put(fme_br->br);
	return ERR_PTR(ret);
}

/**
 * dfl_fme_destroy_bridge - destroy fpga bridge platform device
 * @fme_br: fme bridge to destroy
 */
static void dfl_fme_destroy_bridge(struct dfl_fme_bridge *fme_br)
{
	platform_device_unregister(fme_br->br);
}

/**
 * dfl_fme_destroy_bridge - destroy all fpga bridge platform device
 * @pdata: fme platform device's pdata
 */
static void dfl_fme_destroy_bridges(struct dfl_feature_platform_data *pdata)
{
	struct dfl_fme *priv = dfl_fpga_pdata_get_private(pdata);
	struct dfl_fme_bridge *fbridge, *tmp;

	list_for_each_entry_safe(fbridge, tmp, &priv->bridge_list, node) {
		list_del(&fbridge->node);
		dfl_fme_destroy_bridge(fbridge);
	}
}

/**
 * dfl_fme_create_region - create fpga region platform device as child
 *
 * @pdata: fme platform device's pdata
 * @mgr: mgr platform device needed for region
 * @br: br platform device needed for region
 * @port_id: port id
 *
 * Return: fme region if successful, and error code otherwise.
 */
static struct dfl_fme_region *
dfl_fme_create_region(struct dfl_feature_platform_data *pdata,
		      struct platform_device *mgr,
		      struct platform_device *br, int port_id)
{
	struct dfl_fme_region_pdata region_pdata;
	struct device *dev = &pdata->dev->dev;
	struct dfl_fme_region *fme_region;
	int ret = -ENOMEM;

	fme_region = devm_kzalloc(dev, sizeof(*fme_region), GFP_KERNEL);
	if (!fme_region)
		return ERR_PTR(ret);

	region_pdata.mgr = mgr;
	region_pdata.br = br;

	/*
	 * Each FPGA device may have more than one port, so allocate platform
	 * device using the same port platform device id.
	 */
	fme_region->region = platform_device_alloc(DFL_FPGA_FME_REGION, br->id);
	if (!fme_region->region)
		return ERR_PTR(ret);

	fme_region->region->dev.parent = dev;

	ret = platform_device_add_data(fme_region->region, &region_pdata,
				       sizeof(region_pdata));
	if (ret)
		goto create_region_err;

	ret = platform_device_add(fme_region->region);
	if (ret)
		goto create_region_err;

	fme_region->port_id = port_id;

	return fme_region;

create_region_err:
	platform_device_put(fme_region->region);
	return ERR_PTR(ret);
}

/**
 * dfl_fme_destroy_region - destroy fme region
 * @fme_region: fme region to destroy
 */
static void dfl_fme_destroy_region(struct dfl_fme_region *fme_region)
{
	platform_device_unregister(fme_region->region);
}

/**
 * dfl_fme_destroy_regions - destroy all fme regions
 * @pdata: fme platform device's pdata
 */
static void dfl_fme_destroy_regions(struct dfl_feature_platform_data *pdata)
{
	struct dfl_fme *priv = dfl_fpga_pdata_get_private(pdata);
	struct dfl_fme_region *fme_region, *tmp;

	list_for_each_entry_safe(fme_region, tmp, &priv->region_list, node) {
		list_del(&fme_region->node);
		dfl_fme_destroy_region(fme_region);
	}
}

static int pr_mgmt_init(struct platform_device *pdev,
			struct dfl_feature *feature)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct dfl_fme_region *fme_region;
	struct dfl_fme_bridge *fme_br;
	struct platform_device *mgr;
	struct dfl_fme *priv;
	void __iomem *fme_hdr;
	int ret = -ENODEV, i = 0;
	u64 fme_cap, port_offset;

	fme_hdr = dfl_get_feature_ioaddr_by_id(&pdev->dev,
					       FME_FEATURE_ID_HEADER);

	mutex_lock(&pdata->lock);
	priv = dfl_fpga_pdata_get_private(pdata);

	/* Initialize the region and bridge sub device list */
	INIT_LIST_HEAD(&priv->region_list);
	INIT_LIST_HEAD(&priv->bridge_list);

	/* Create fpga mgr platform device */
	mgr = dfl_fme_create_mgr(pdata, feature);
	if (IS_ERR(mgr)) {
		dev_err(&pdev->dev, "fail to create fpga mgr pdev\n");
		goto unlock;
	}

	priv->mgr = mgr;

	/* Read capability register to check number of regions and bridges */
	fme_cap = readq(fme_hdr + FME_HDR_CAP);
	for (; i < FIELD_GET(FME_CAP_NUM_PORTS, fme_cap); i++) {
		port_offset = readq(fme_hdr + FME_HDR_PORT_OFST(i));
		if (!(port_offset & FME_PORT_OFST_IMP))
			continue;

		/* Create bridge for each port */
		fme_br = dfl_fme_create_bridge(pdata, i);
		if (IS_ERR(fme_br)) {
			ret = PTR_ERR(fme_br);
			goto destroy_region;
		}

		list_add(&fme_br->node, &priv->bridge_list);

		/* Create region for each port */
		fme_region = dfl_fme_create_region(pdata, mgr,
						   fme_br->br, i);
		if (IS_ERR(fme_region)) {
			ret = PTR_ERR(fme_region);
			goto destroy_region;
		}

		list_add(&fme_region->node, &priv->region_list);
	}
	mutex_unlock(&pdata->lock);

	return 0;

destroy_region:
	dfl_fme_destroy_regions(pdata);
	dfl_fme_destroy_bridges(pdata);
	dfl_fme_destroy_mgr(pdata);
unlock:
	mutex_unlock(&pdata->lock);
	return ret;
}

static void pr_mgmt_uinit(struct platform_device *pdev,
			  struct dfl_feature *feature)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);

	mutex_lock(&pdata->lock);

	dfl_fme_destroy_regions(pdata);
	dfl_fme_destroy_bridges(pdata);
	dfl_fme_destroy_mgr(pdata);
	mutex_unlock(&pdata->lock);
}

static long fme_pr_ioctl(struct platform_device *pdev,
			 struct dfl_feature *feature,
			 unsigned int cmd, unsigned long arg)
{
	long ret;

	switch (cmd) {
	case DFL_FPGA_FME_PORT_PR:
		ret = fme_pr(pdev, arg);
		break;
	default:
		ret = -ENODEV;
	}

	return ret;
}

const struct dfl_feature_id fme_pr_mgmt_id_table[] = {
	{.id = FME_FEATURE_ID_PR_MGMT,},
	{0}
};

const struct dfl_feature_ops fme_pr_mgmt_ops = {
	.init = pr_mgmt_init,
	.uinit = pr_mgmt_uinit,
	.ioctl = fme_pr_ioctl,
};
