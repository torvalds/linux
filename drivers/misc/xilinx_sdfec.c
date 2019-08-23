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
 * struct xsdfec_clks - For managing SD-FEC clocks
 * @core_clk: Main processing clock for core
 * @axi_clk: AXI4-Lite memory-mapped clock
 * @din_words_clk: DIN Words AXI4-Stream Slave clock
 * @din_clk: DIN AXI4-Stream Slave clock
 * @dout_clk: DOUT Words AXI4-Stream Slave clock
 * @dout_words_clk: DOUT AXI4-Stream Slave clock
 * @ctrl_clk: Control AXI4-Stream Slave clock
 * @status_clk: Status AXI4-Stream Slave clock
 */
struct xsdfec_clks {
	struct clk *core_clk;
	struct clk *axi_clk;
	struct clk *din_words_clk;
	struct clk *din_clk;
	struct clk *dout_clk;
	struct clk *dout_words_clk;
	struct clk *ctrl_clk;
	struct clk *status_clk;
};

/**
 * struct xsdfec_dev - Driver data for SDFEC
 * @regs: device physical base address
 * @dev: pointer to device struct
 * @miscdev: Misc device handle
 * @error_data_lock: Error counter and states spinlock
 * @clks: Clocks managed by the SDFEC driver
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
	struct xsdfec_clks clks;
	char dev_name[DEV_NAME_LEN];
	int dev_id;
};

static const struct file_operations xsdfec_fops = {
	.owner = THIS_MODULE,
};

static int xsdfec_clk_init(struct platform_device *pdev,
			   struct xsdfec_clks *clks)
{
	int err;

	clks->core_clk = devm_clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(clks->core_clk)) {
		dev_err(&pdev->dev, "failed to get core_clk");
		return PTR_ERR(clks->core_clk);
	}

	clks->axi_clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(clks->axi_clk)) {
		dev_err(&pdev->dev, "failed to get axi_clk");
		return PTR_ERR(clks->axi_clk);
	}

	clks->din_words_clk = devm_clk_get(&pdev->dev, "s_axis_din_words_aclk");
	if (IS_ERR(clks->din_words_clk)) {
		if (PTR_ERR(clks->din_words_clk) != -ENOENT) {
			err = PTR_ERR(clks->din_words_clk);
			return err;
		}
		clks->din_words_clk = NULL;
	}

	clks->din_clk = devm_clk_get(&pdev->dev, "s_axis_din_aclk");
	if (IS_ERR(clks->din_clk)) {
		if (PTR_ERR(clks->din_clk) != -ENOENT) {
			err = PTR_ERR(clks->din_clk);
			return err;
		}
		clks->din_clk = NULL;
	}

	clks->dout_clk = devm_clk_get(&pdev->dev, "m_axis_dout_aclk");
	if (IS_ERR(clks->dout_clk)) {
		if (PTR_ERR(clks->dout_clk) != -ENOENT) {
			err = PTR_ERR(clks->dout_clk);
			return err;
		}
		clks->dout_clk = NULL;
	}

	clks->dout_words_clk =
		devm_clk_get(&pdev->dev, "s_axis_dout_words_aclk");
	if (IS_ERR(clks->dout_words_clk)) {
		if (PTR_ERR(clks->dout_words_clk) != -ENOENT) {
			err = PTR_ERR(clks->dout_words_clk);
			return err;
		}
		clks->dout_words_clk = NULL;
	}

	clks->ctrl_clk = devm_clk_get(&pdev->dev, "s_axis_ctrl_aclk");
	if (IS_ERR(clks->ctrl_clk)) {
		if (PTR_ERR(clks->ctrl_clk) != -ENOENT) {
			err = PTR_ERR(clks->ctrl_clk);
			return err;
		}
		clks->ctrl_clk = NULL;
	}

	clks->status_clk = devm_clk_get(&pdev->dev, "m_axis_status_aclk");
	if (IS_ERR(clks->status_clk)) {
		if (PTR_ERR(clks->status_clk) != -ENOENT) {
			err = PTR_ERR(clks->status_clk);
			return err;
		}
		clks->status_clk = NULL;
	}

	err = clk_prepare_enable(clks->core_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable core_clk (%d)", err);
		return err;
	}

	err = clk_prepare_enable(clks->axi_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable axi_clk (%d)", err);
		goto err_disable_core_clk;
	}

	err = clk_prepare_enable(clks->din_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable din_clk (%d)", err);
		goto err_disable_axi_clk;
	}

	err = clk_prepare_enable(clks->din_words_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable din_words_clk (%d)", err);
		goto err_disable_din_clk;
	}

	err = clk_prepare_enable(clks->dout_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable dout_clk (%d)", err);
		goto err_disable_din_words_clk;
	}

	err = clk_prepare_enable(clks->dout_words_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable dout_words_clk (%d)",
			err);
		goto err_disable_dout_clk;
	}

	err = clk_prepare_enable(clks->ctrl_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable ctrl_clk (%d)", err);
		goto err_disable_dout_words_clk;
	}

	err = clk_prepare_enable(clks->status_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable status_clk (%d)\n", err);
		goto err_disable_ctrl_clk;
	}

	return err;

err_disable_ctrl_clk:
	clk_disable_unprepare(clks->ctrl_clk);
err_disable_dout_words_clk:
	clk_disable_unprepare(clks->dout_words_clk);
err_disable_dout_clk:
	clk_disable_unprepare(clks->dout_clk);
err_disable_din_words_clk:
	clk_disable_unprepare(clks->din_words_clk);
err_disable_din_clk:
	clk_disable_unprepare(clks->din_clk);
err_disable_axi_clk:
	clk_disable_unprepare(clks->axi_clk);
err_disable_core_clk:
	clk_disable_unprepare(clks->core_clk);

	return err;
}

static void xsdfec_disable_all_clks(struct xsdfec_clks *clks)
{
	clk_disable_unprepare(clks->status_clk);
	clk_disable_unprepare(clks->ctrl_clk);
	clk_disable_unprepare(clks->dout_words_clk);
	clk_disable_unprepare(clks->dout_clk);
	clk_disable_unprepare(clks->din_words_clk);
	clk_disable_unprepare(clks->din_clk);
	clk_disable_unprepare(clks->core_clk);
	clk_disable_unprepare(clks->axi_clk);
}

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

	err = xsdfec_clk_init(pdev, &xsdfec->clks);
	if (err)
		return err;

	dev = xsdfec->dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xsdfec->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(xsdfec->regs)) {
		err = PTR_ERR(xsdfec->regs);
		goto err_xsdfec_dev;
	}

	/* Save driver private data */
	platform_set_drvdata(pdev, xsdfec);

	mutex_lock(&dev_idr_lock);
	err = idr_alloc(&dev_idr, xsdfec->dev_name, 0, 0, GFP_KERNEL);
	mutex_unlock(&dev_idr_lock);
	if (err < 0)
		goto err_xsdfec_dev;
	xsdfec->dev_id = err;

	snprintf(xsdfec->dev_name, DEV_NAME_LEN, "xsdfec%d", xsdfec->dev_id);
	xsdfec->miscdev.minor = MISC_DYNAMIC_MINOR;
	xsdfec->miscdev.name = xsdfec->dev_name;
	xsdfec->miscdev.fops = &xsdfec_fops;
	xsdfec->miscdev.parent = dev;
	err = misc_register(&xsdfec->miscdev);
	if (err) {
		dev_err(dev, "error:%d. Unable to register device", err);
		goto err_xsdfec_idr;
	}
	return 0;

err_xsdfec_idr:
	xsdfec_idr_remove(xsdfec);
err_xsdfec_dev:
	xsdfec_disable_all_clks(&xsdfec->clks);
	return err;
}

static int xsdfec_remove(struct platform_device *pdev)
{
	struct xsdfec_dev *xsdfec;

	xsdfec = platform_get_drvdata(pdev);
	misc_deregister(&xsdfec->miscdev);
	xsdfec_idr_remove(xsdfec);
	xsdfec_disable_all_clks(&xsdfec->clks);
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
