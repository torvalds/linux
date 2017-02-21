/**
 * dwc3-rockchip-inno.c - Rockchip DWC3 Specific Glue layer with INNO PHY
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 *
 * Authors: William Wu <william.wu@rock-chips.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/notifier.h>
#include <linux/uaccess.h>
#include <linux/usb/phy.h>
#include <linux/usb/ch9.h>

#include "core.h"
#include "../host/xhci.h"

#define XHCI_TSTCTRL_MASK		(0xf << 28)
struct dwc3_rockchip {
	struct device		*dev;
	struct clk		**clks;
	int			num_clocks;
	struct dwc3		*dwc;
	struct dentry		*root;
	struct usb_phy		*phy;
	struct notifier_block	u3phy_nb;
	struct work_struct      u3_work;
	struct mutex		lock;
};

static int dwc3_rockchip_host_testmode_show(struct seq_file *s, void *unused)
{
	struct dwc3_rockchip	*rockchip = s->private;
	struct dwc3		*dwc = rockchip->dwc;
	struct usb_hcd		*hcd  = dev_get_drvdata(&dwc->xhci->dev);
	struct xhci_hcd		*xhci = hcd_to_xhci(hcd);
	__le32 __iomem		**port_array;
	u32			reg;

	if (rockchip->dwc->dr_mode == USB_DR_MODE_PERIPHERAL) {
		dev_warn(rockchip->dev, "USB HOST not support!\n");
		return 0;
	}

	if (hcd->state == HC_STATE_HALT) {
		dev_warn(rockchip->dev, "HOST is halted!\n");
		return 0;
	}

	port_array = xhci->usb2_ports;
	reg = readl(port_array[0] + 1);
	reg &= XHCI_TSTCTRL_MASK;
	reg >>= 28;

	switch (reg) {
	case 0:
		seq_puts(s, "U2: no test\n");
		break;
	case TEST_J:
		seq_puts(s, "U2: test_j\n");
		break;
	case TEST_K:
		seq_puts(s, "U2: test_k\n");
		break;
	case TEST_SE0_NAK:
		seq_puts(s, "U2: test_se0_nak\n");
		break;
	case TEST_PACKET:
		seq_puts(s, "U2: test_packet\n");
		break;
	case TEST_FORCE_EN:
		seq_puts(s, "U2: test_force_enable\n");
		break;
	default:
		seq_printf(s, "U2: UNKNOWN %d\n", reg);
	}

	port_array = xhci->usb3_ports;
	reg = readl(port_array[0]);
	reg &= PORT_PLS_MASK;
	if (reg == USB_SS_PORT_LS_COMP_MOD)
		seq_puts(s, "U3: compliance mode\n");
	else
		seq_printf(s, "U3: UNKNOWN %d\n", reg >> 5);

	return 0;
}

static int dwc3_rockchip_host_testmode_open(struct inode *inode,
					    struct file *file)
{
	return single_open(file, dwc3_rockchip_host_testmode_show,
			   inode->i_private);
}

/**
 * dwc3_rockchip_set_test_mode - Enables USB2/USB3 HOST Test Modes
 * @rockchip: pointer to our context structure
 * @mode: the mode to set (U2: J, K SE0 NAK, Test_packet,
 * Force Enable; U3: Compliance mode)
 *
 * This function will return 0 on success or -EINVAL if wrong Test
 * Selector is passed.
 */
static int dwc3_rockchip_set_test_mode(struct dwc3_rockchip *rockchip,
				       u32 mode)
{
	struct dwc3	*dwc = rockchip->dwc;
	struct usb_hcd	*hcd  = dev_get_drvdata(&dwc->xhci->dev);
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	__le32 __iomem	**port_array;
	int		ret;
	u32		reg;

	if (hcd->state == HC_STATE_HALT) {
		dev_err(rockchip->dev, "HOST is halted!\n");
		return -EINVAL;
	}

	switch (mode) {
	case TEST_J:
	case TEST_K:
	case TEST_SE0_NAK:
	case TEST_PACKET:
	case TEST_FORCE_EN:
		port_array = xhci->usb2_ports;
		reg = readl(port_array[0] + 1);
		reg &= ~XHCI_TSTCTRL_MASK;
		reg |= mode << 28;
		writel(reg, port_array[0] + 1);
		break;
	case USB_SS_PORT_LS_COMP_MOD:
		/*
		 * Enable Inno U3 PHY to toggle CP test pattern
		 * before set XHCI controller enter compliance mode.
		 */
		ret = phy_cp_test(dwc->usb3_generic_phy);
		if (ret) {
			dev_err(rockchip->dev, "phy cp test fail!\n");
			return ret;
		}

		port_array = xhci->usb3_ports;
		xhci_set_link_state(xhci, port_array, 0, mode);
		break;
	default:
		return -EINVAL;
	}

	dev_info(rockchip->dev, "set USB HOST test mode successfully!\n");

	return 0;
}

static ssize_t dwc3_rockchip_host_testmode_write(struct file *file,
						 const char __user *ubuf,
						 size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct dwc3_rockchip	*rockchip = s->private;
	u32			testmode = 0;
	char			buf[32];

	if (rockchip->dwc->dr_mode == USB_DR_MODE_PERIPHERAL) {
		dev_warn(rockchip->dev, "USB HOST not support!\n");
		return -EINVAL;
	}

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "test_j", 6)) {
		testmode = TEST_J;
	} else if (!strncmp(buf, "test_k", 6)) {
		testmode = TEST_K;
	} else if (!strncmp(buf, "test_se0_nak", 12)) {
		testmode = TEST_SE0_NAK;
	} else if (!strncmp(buf, "test_packet", 11)) {
		testmode = TEST_PACKET;
	} else if (!strncmp(buf, "test_force_enable", 17)) {
		testmode = TEST_FORCE_EN;
	} else if (!strncmp(buf, "test_u3", 7)) {
		testmode = USB_SS_PORT_LS_COMP_MOD;
	} else {
		dev_warn(rockchip->dev, "Test cmd not support!\n");
		return -EINVAL;
	}

	dwc3_rockchip_set_test_mode(rockchip, testmode);

	return count;
}

static const struct file_operations dwc3_host_testmode_fops = {
	.open			= dwc3_rockchip_host_testmode_open,
	.write			= dwc3_rockchip_host_testmode_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static void dwc3_rockchip_debugfs_init(struct dwc3_rockchip *rockchip)
{
	struct dentry   *root;
	struct dentry   *file;

	root = debugfs_create_dir(dev_name(rockchip->dev), NULL);
	if (IS_ERR_OR_NULL(root)) {
		if (!root)
			dev_err(rockchip->dev, "Can't create debugfs root\n");
		return;
	}
	rockchip->root = root;

	if (IS_ENABLED(CONFIG_USB_DWC3_DUAL_ROLE) ||
	    IS_ENABLED(CONFIG_USB_DWC3_HOST)) {
		file = debugfs_create_file("host_testmode", S_IRUSR | S_IWUSR,
					   root, rockchip,
					   &dwc3_host_testmode_fops);
		if (!file)
			dev_dbg(rockchip->dev, "Can't create debugfs host_testmode\n");
	}
}

static int u3phy_disconnect_det_notifier(struct notifier_block *nb,
					 unsigned long event, void *p)
{
	struct dwc3_rockchip *rockchip =
		container_of(nb, struct dwc3_rockchip, u3phy_nb);

	schedule_work(&rockchip->u3_work);

	return NOTIFY_DONE;
}

static void u3phy_disconnect_det_work(struct work_struct *work)
{
	struct dwc3_rockchip *rockchip =
		container_of(work, struct dwc3_rockchip, u3_work);
	struct usb_hcd	*hcd = dev_get_drvdata(&rockchip->dwc->xhci->dev);
	struct usb_hcd	*shared_hcd = hcd->shared_hcd;
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	u32		count = 0;

	mutex_lock(&rockchip->lock);

	if (hcd->state != HC_STATE_HALT) {
		usb_remove_hcd(shared_hcd);
		usb_remove_hcd(hcd);
	}

	if (rockchip->phy)
		usb_phy_shutdown(rockchip->phy);

	while (hcd->state != HC_STATE_HALT) {
		if (++count > 1000) {
			dev_err(rockchip->dev,
				"wait for HCD remove 1s timeout!\n");
			break;
		}
		usleep_range(1000, 1100);
	}

	if (hcd->state == HC_STATE_HALT) {
		xhci->shared_hcd = shared_hcd;
		usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
		usb_add_hcd(shared_hcd, hcd->irq, IRQF_SHARED);
	}

	if (rockchip->phy)
		usb_phy_init(rockchip->phy);

	mutex_unlock(&rockchip->lock);
}

static int dwc3_rockchip_probe(struct platform_device *pdev)
{
	struct dwc3_rockchip	*rockchip;
	struct device		*dev = &pdev->dev;
	struct device_node	*np = dev->of_node, *child;
	struct platform_device	*child_pdev;

	unsigned int		count;
	int			ret;
	int			i;

	rockchip = devm_kzalloc(dev, sizeof(*rockchip), GFP_KERNEL);
	if (!rockchip)
		return -ENOMEM;

	child = of_get_child_by_name(np, "dwc3");
	if (!child) {
		dev_err(dev, "failed to find dwc3 core node\n");
		return -ENODEV;
	}

	count = of_clk_get_parent_count(np);
	if (!count)
		return -ENOENT;

	rockchip->num_clocks = count;

	rockchip->clks = devm_kcalloc(dev, rockchip->num_clocks,
			sizeof(struct clk *), GFP_KERNEL);
	if (!rockchip->clks)
		return -ENOMEM;

	platform_set_drvdata(pdev, rockchip);
	rockchip->dev = dev;

	for (i = 0; i < rockchip->num_clocks; i++) {
		struct clk	*clk;

		clk = of_clk_get(np, i);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			goto err0;
		}

		ret = clk_prepare_enable(clk);
		if (ret < 0) {
			clk_put(clk);
			goto err0;
		}

		rockchip->clks[i] = clk;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "get_sync failed with err %d\n", ret);
		goto err1;
	}

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret)
		goto err1;

	child_pdev = of_find_device_by_node(child);
	if (!child_pdev) {
		dev_err(dev, "failed to find dwc3 core device\n");
		ret = -ENODEV;
		goto err2;
	}

	rockchip->dwc = platform_get_drvdata(child_pdev);
	if (!rockchip->dwc || !rockchip->dwc->xhci) {
		dev_dbg(dev, "failed to get drvdata dwc3\n");
		ret = -EPROBE_DEFER;
		goto err2;
	}

	mutex_init(&rockchip->lock);

	rockchip->phy = devm_usb_get_phy(dev, USB_PHY_TYPE_USB3);
	if (rockchip->phy) {
		INIT_WORK(&rockchip->u3_work, u3phy_disconnect_det_work);
		rockchip->u3phy_nb.notifier_call =
			u3phy_disconnect_det_notifier;
		usb_register_notifier(rockchip->phy, &rockchip->u3phy_nb);
	}

	dwc3_rockchip_debugfs_init(rockchip);

	return 0;

err2:
	of_platform_depopulate(dev);
err1:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
err0:
	for (i = 0; i < rockchip->num_clocks && rockchip->clks[i]; i++) {
		if (!pm_runtime_status_suspended(dev))
			clk_disable(rockchip->clks[i]);
		clk_unprepare(rockchip->clks[i]);
		clk_put(rockchip->clks[i]);
	}

	return ret;
}

static int dwc3_rockchip_remove(struct platform_device *pdev)
{
	struct dwc3_rockchip	*rockchip = platform_get_drvdata(pdev);
	struct device		*dev = &pdev->dev;
	int			i;

	of_platform_depopulate(dev);

	debugfs_remove_recursive(rockchip->root);

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	for (i = 0; i < rockchip->num_clocks; i++) {
		if (!pm_runtime_status_suspended(dev))
			clk_disable(rockchip->clks[i]);
		clk_unprepare(rockchip->clks[i]);
		clk_put(rockchip->clks[i]);
	}

	return 0;
}

#ifdef CONFIG_PM
static int dwc3_rockchip_runtime_suspend(struct device *dev)
{
	struct dwc3_rockchip	*rockchip = dev_get_drvdata(dev);
	int			i;

	for (i = 0; i < rockchip->num_clocks; i++)
		clk_disable(rockchip->clks[i]);

	return 0;
}

static int dwc3_rockchip_runtime_resume(struct device *dev)
{
	struct dwc3_rockchip	*rockchip = dev_get_drvdata(dev);
	int			ret;
	int			i;

	for (i = 0; i < rockchip->num_clocks; i++) {
		ret = clk_enable(rockchip->clks[i]);
		if (ret < 0) {
			while (--i >= 0)
				clk_disable(rockchip->clks[i]);
			return ret;
		}
	}

	return 0;
}

static const struct dev_pm_ops dwc3_rockchip_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(dwc3_rockchip_runtime_suspend,
			   dwc3_rockchip_runtime_resume, NULL)
};

#define DEV_PM_OPS	(&dwc3_rockchip_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM */

static const struct of_device_id rockchip_dwc3_match[] = {
	{ .compatible = "rockchip,rk3328-dwc3" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, rockchip_dwc3_match);

static struct platform_driver dwc3_rockchip_driver = {
	.probe		= dwc3_rockchip_probe,
	.remove		= dwc3_rockchip_remove,
	.driver		= {
		.name	= "rockchip-inno-dwc3",
		.pm	= DEV_PM_OPS,
		.of_match_table = rockchip_dwc3_match,
	},
};

module_platform_driver(dwc3_rockchip_driver);

MODULE_ALIAS("platform:rockchip-inno-dwc3");
MODULE_AUTHOR("William Wu <william.wu@rock-chips.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 rockchip-inno Glue Layer");
