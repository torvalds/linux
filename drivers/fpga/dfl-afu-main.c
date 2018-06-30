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

/**
 * port_enable - enable a port
 * @pdev: port platform device.
 *
 * Enable Port by clear the port soft reset bit, which is set by default.
 * The User AFU is unable to respond to any MMIO access while in reset.
 * port_enable function should only be used after port_disable
 * function.
 */
static void port_enable(struct platform_device *pdev)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	void __iomem *base;
	u64 v;

	WARN_ON(!pdata->disable_count);

	if (--pdata->disable_count != 0)
		return;

	base = dfl_get_feature_ioaddr_by_id(&pdev->dev, PORT_FEATURE_ID_HEADER);

	/* Clear port soft reset */
	v = readq(base + PORT_HDR_CTRL);
	v &= ~PORT_CTRL_SFTRST;
	writeq(v, base + PORT_HDR_CTRL);
}

#define RST_POLL_INVL 10 /* us */
#define RST_POLL_TIMEOUT 1000 /* us */

/**
 * port_disable - disable a port
 * @pdev: port platform device.
 *
 * Disable Port by setting the port soft reset bit, it puts the port into
 * reset.
 */
static int port_disable(struct platform_device *pdev)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	void __iomem *base;
	u64 v;

	if (pdata->disable_count++ != 0)
		return 0;

	base = dfl_get_feature_ioaddr_by_id(&pdev->dev, PORT_FEATURE_ID_HEADER);

	/* Set port soft reset */
	v = readq(base + PORT_HDR_CTRL);
	v |= PORT_CTRL_SFTRST;
	writeq(v, base + PORT_HDR_CTRL);

	/*
	 * HW sets ack bit to 1 when all outstanding requests have been drained
	 * on this port and minimum soft reset pulse width has elapsed.
	 * Driver polls port_soft_reset_ack to determine if reset done by HW.
	 */
	if (readq_poll_timeout(base + PORT_HDR_CTRL, v, v & PORT_CTRL_SFTRST,
			       RST_POLL_INVL, RST_POLL_TIMEOUT)) {
		dev_err(&pdev->dev, "timeout, fail to reset device\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int port_get_id(struct platform_device *pdev)
{
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(&pdev->dev, PORT_FEATURE_ID_HEADER);

	return FIELD_GET(PORT_CAP_PORT_NUM, readq(base + PORT_HDR_CAP));
}

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

static int port_enable_set(struct platform_device *pdev, bool enable)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	int ret = 0;

	mutex_lock(&pdata->lock);
	if (enable)
		port_enable(pdev);
	else
		ret = port_disable(pdev);
	mutex_unlock(&pdata->lock);

	return ret;
}

static struct dfl_fpga_port_ops afu_port_ops = {
	.name = DFL_FPGA_FEATURE_DEV_PORT,
	.owner = THIS_MODULE,
	.get_id = port_get_id,
	.enable_set = port_enable_set,
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

static int __init afu_init(void)
{
	int ret;

	dfl_fpga_port_ops_add(&afu_port_ops);

	ret = platform_driver_register(&afu_driver);
	if (ret)
		dfl_fpga_port_ops_del(&afu_port_ops);

	return ret;
}

static void __exit afu_exit(void)
{
	platform_driver_unregister(&afu_driver);

	dfl_fpga_port_ops_del(&afu_port_ops);
}

module_init(afu_init);
module_exit(afu_exit);

MODULE_DESCRIPTION("FPGA Accelerated Function Unit driver");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dfl-port");
