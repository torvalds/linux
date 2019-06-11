// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx SDFEC
 *
 * Copyright (C) 2019 Xilinx, Inc.
 *
 * Description:
 * This driver is developed for SDFEC16 (Soft Decision FEC 16nm)
 * IP. It exposes a char device which supports file operations
 * like  open(), close() and ioctl().
 */

#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/clk.h>

#define DEV_NAME_LEN 12

static struct idr dev_idr;
static struct mutex dev_idr_lock;

/**
 * struct xsdfec_dev - Driver data for SDFEC
 * @regs: device physical base address
 * @dev: pointer to device struct
 * @miscdev: Misc device handle
 * @error_data_lock: Error counter and states spinlock
 * @dev_name: Device name
 * @dev_id: Device ID
 *
 * This structure contains necessary state for SDFEC driver to operate
 */
struct xsdfec_dev {
	void __iomem *regs;
	struct device *dev;
	struct miscdevice miscdev;
	/* Spinlock to protect state_updated and stats_updated */
	spinlock_t error_data_lock;
	char dev_name[DEV_NAME_LEN];
	int dev_id;
};

static const struct file_operations xsdfec_fops = {
	.owner = THIS_MODULE,
};

static void xsdfec_idr_remove(struct xsdfec_dev *xsdfec)
{
	mutex_lock(&dev_idr_lock);
	idr_remove(&dev_idr, xsdfec->dev_id);
	mutex_unlock(&dev_idr_lock);
}

static int xsdfec_probe(struct platform_device *pdev)
{
	struct xsdfec_dev *xsdfec;
	struct device *dev;
	struct resource *res;
	int err;

	xsdfec = devm_kzalloc(&pdev->dev, sizeof(*xsdfec), GFP_KERNEL);
	if (!xsdfec)
		return -ENOMEM;

	xsdfec->dev = &pdev->dev;
	spin_lock_init(&xsdfec->error_data_lock);

	dev = xsdfec->dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xsdfec->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(xsdfec->regs)) {
		err = PTR_ERR(xsdfec->regs);
		return err;
	}

	/* Save driver private data */
	platform_set_drvdata(pdev, xsdfec);

	mutex_lock(&dev_idr_lock);
	err = idr_alloc(&dev_idr, xsdfec->dev_name, 0, 0, GFP_KERNEL);
	mutex_unlock(&dev_idr_lock);
	if (err < 0)
		goto err_xsddev_idr;
	xsdfec->dev_id = err;

	snprintf(xsdfec->dev_name, DEV_NAME_LEN, "xsdfec%d", xsdfec->dev_id);
	xsdfec->miscdev.minor = MISC_DYNAMIC_MINOR;
	xsdfec->miscdev.name = xsdfec->dev_name;
	xsdfec->miscdev.fops = &xsdfec_fops;
	xsdfec->miscdev.parent = dev;
	err = misc_register(&xsdfec->miscdev);
	if (err) {
		dev_err(dev, "error:%d. Unable to register device", err);
		return err;
	}
	return 0;

err_xsddev_idr:
	xsdfec_idr_remove(xsdfec);

	return err;
}

static int xsdfec_remove(struct platform_device *pdev)
{
	struct xsdfec_dev *xsdfec;

	xsdfec = platform_get_drvdata(pdev);
	misc_deregister(&xsdfec->miscdev);
	xsdfec_idr_remove(xsdfec);
	return 0;
}

static const struct of_device_id xsdfec_of_match[] = {
	{
		.compatible = "xlnx,sd-fec-1.1",
	},
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, xsdfec_of_match);

static struct platform_driver xsdfec_driver = {
	.driver = {
		.name = "xilinx-sdfec",
		.of_match_table = xsdfec_of_match,
	},
	.probe = xsdfec_probe,
	.remove =  xsdfec_remove,
};

static int __init xsdfec_init(void)
{
	int err;

	mutex_init(&dev_idr_lock);
	idr_init(&dev_idr);
	err = platform_driver_register(&xsdfec_driver);
	if (err < 0) {
		pr_err("%s Unabled to register SDFEC driver", __func__);
		return err;
	}
	return 0;
}

static void __exit xsdfec_exit(void)
{
	platform_driver_unregister(&xsdfec_driver);
	idr_destroy(&dev_idr);
}

module_init(xsdfec_init);
module_exit(xsdfec_exit);

MODULE_AUTHOR("Xilinx, Inc");
MODULE_DESCRIPTION("Xilinx SD-FEC16 Driver");
MODULE_LICENSE("GPL");
