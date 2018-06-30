// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for FPGA Accelerated Function Unit (AFU)
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *   Wu Hao <hao.wu@intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *   Joseph Grecco <joe.grecco@intel.com>
 *   Enno Luebbers <enno.luebbers@intel.com>
 *   Tim Whisonant <tim.whisonant@intel.com>
 *   Ananda Ravuri <ananda.ravuri@intel.com>
 *   Henry Mitchel <henry.mitchel@intel.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "dfl.h"

static int port_hdr_init(struct platform_device *pdev,
			 struct dfl_feature *feature)
{
	dev_dbg(&pdev->dev, "PORT HDR Init.\n");

	return 0;
}

static void port_hdr_uinit(struct platform_device *pdev,
			   struct dfl_feature *feature)
{
	dev_dbg(&pdev->dev, "PORT HDR UInit.\n");
}

static const struct dfl_feature_ops port_hdr_ops = {
	.init = port_hdr_init,
	.uinit = port_hdr_uinit,
};

static struct dfl_feature_driver port_feature_drvs[] = {
	{
		.id = PORT_FEATURE_ID_HEADER,
		.ops = &port_hdr_ops,
	},
	{
		.ops = NULL,
	}
};

static int afu_open(struct inode *inode, struct file *filp)
{
	struct platform_device *fdev = dfl_fpga_inode_to_feature_dev(inode);
	struct dfl_feature_platform_data *pdata;
	int ret;

	pdata = dev_get_platdata(&fdev->dev);
	if (WARN_ON(!pdata))
		return -ENODEV;

	ret = dfl_feature_dev_use_begin(pdata);
	if (ret)
		return ret;

	dev_dbg(&fdev->dev, "Device File Open\n");
	filp->private_data = fdev;

	return 0;
}

static int afu_release(struct inode *inode, struct file *filp)
{
	struct platform_device *pdev = filp->private_data;
	struct dfl_feature_platform_data *pdata;

	dev_dbg(&pdev->dev, "Device File Release\n");

	pdata = dev_get_platdata(&pdev->dev);

	dfl_feature_dev_use_end(pdata);

	return 0;
}

static long afu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct platform_device *pdev = filp->private_data;
	struct dfl_feature_platform_data *pdata;
	struct dfl_feature *f;
	long ret;

	dev_dbg(&pdev->dev, "%s cmd 0x%x\n", __func__, cmd);

	pdata = dev_get_platdata(&pdev->dev);

	switch (cmd) {
	default:
		/*
		 * Let sub-feature's ioctl function to handle the cmd
		 * Sub-feature's ioctl returns -ENODEV when cmd is not
		 * handled in this sub feature, and returns 0 and other
		 * error code if cmd is handled.
		 */
		dfl_fpga_dev_for_each_feature(pdata, f)
			if (f->ops && f->ops->ioctl) {
				ret = f->ops->ioctl(pdev, f, cmd, arg);
				if (ret != -ENODEV)
					return ret;
			}
	}

	return -EINVAL;
}

static const struct file_operations afu_fops = {
	.owner = THIS_MODULE,
	.open = afu_open,
	.release = afu_release,
	.unlocked_ioctl = afu_ioctl,
};

static int afu_probe(struct platform_device *pdev)
{
	int ret;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	ret = dfl_fpga_dev_feature_init(pdev, port_feature_drvs);
	if (ret)
		return ret;

	ret = dfl_fpga_dev_ops_register(pdev, &afu_fops, THIS_MODULE);
	if (ret)
		dfl_fpga_dev_feature_uinit(pdev);

	return ret;
}

static int afu_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "%s\n", __func__);

	dfl_fpga_dev_ops_unregister(pdev);
	dfl_fpga_dev_feature_uinit(pdev);

	return 0;
}

static struct platform_driver afu_driver = {
	.driver	= {
		.name    = DFL_FPGA_FEATURE_DEV_PORT,
	},
	.probe   = afu_probe,
	.remove  = afu_remove,
};

module_platform_driver(afu_driver);

MODULE_DESCRIPTION("FPGA Accelerated Function Unit driver");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dfl-port");
