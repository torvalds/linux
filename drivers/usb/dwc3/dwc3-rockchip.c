/**
 * dwc3-rockchip.c - Rockchip Specific Glue layer
 *
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 *
 * Authors: William Wu <william.wu@rock-chips.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/extcon.h>
#include <linux/freezer.h>
#include <linux/iopoll.h>
#include <linux/reset.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/ch9.h>

#include "core.h"
#include "io.h"
#include "../host/xhci.h"

#define DWC3_ROCKCHIP_AUTOSUSPEND_DELAY	500 /* ms */
#define PERIPHERAL_DISCONNECT_TIMEOUT	1000000 /* us */
#define WAIT_FOR_HCD_READY_TIMEOUT	5000000 /* us */
#define XHCI_TSTCTRL_MASK		(0xf << 28)

struct dwc3_rockchip {
	int			num_clocks;
	bool			connected;
	bool			skip_suspend;
	bool			suspended;
	bool			force_mode;
	enum usb_dr_mode	original_dr_mode;
	struct device		*dev;
	struct clk		**clks;
	struct dwc3		*dwc;
	struct reset_control	*otg_rst;
	struct extcon_dev	*edev;
	struct notifier_block	device_nb;
	struct notifier_block	host_nb;
	struct work_struct	otg_work;
	struct mutex		lock;
};

static ssize_t dwc3_mode_show(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	struct dwc3_rockchip	*rockchip = dev_get_drvdata(device);
	struct dwc3		*dwc = rockchip->dwc;
	int			ret;

	switch (dwc->dr_mode) {
	case USB_DR_MODE_HOST:
		ret = sprintf(buf, "host\n");
		break;
	case USB_DR_MODE_PERIPHERAL:
		ret = sprintf(buf, "peripheral\n");
		break;
	case USB_DR_MODE_OTG:
		ret = sprintf(buf, "otg\n");
		break;
	default:
		ret = sprintf(buf, "UNKNOWN\n");
	}

	return ret;
}

static ssize_t dwc3_mode_store(struct device *device,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct dwc3_rockchip	*rockchip = dev_get_drvdata(device);
	struct dwc3		*dwc = rockchip->dwc;
	enum usb_dr_mode	new_dr_mode;

	if (!rockchip->original_dr_mode)
		rockchip->original_dr_mode = dwc->dr_mode;

	if (rockchip->original_dr_mode != USB_DR_MODE_OTG) {
		dev_err(rockchip->dev, "Not support set mode!\n");
		return -EINVAL;
	}

	if (!strncmp(buf, "0", 1) || !strncmp(buf, "otg", 3)) {
		new_dr_mode = USB_DR_MODE_OTG;
	} else if (!strncmp(buf, "1", 1) || !strncmp(buf, "host", 4)) {
		new_dr_mode = USB_DR_MODE_HOST;
	} else if (!strncmp(buf, "2", 1) || !strncmp(buf, "peripheral", 10)) {
		new_dr_mode = USB_DR_MODE_PERIPHERAL;
	} else {
		dev_info(rockchip->dev, "illegal dr_mode\n");
		return count;
	}

	if (dwc->dr_mode == new_dr_mode) {
		dev_info(rockchip->dev, "Same with current dr_mode\n");
		return count;
	}

	rockchip->force_mode = true;

	/*
	 * If force to host mode from current peripheral mode, firstly,
	 * set the usb2 phy mode to PHY_MODE_INVALID to disable vbus
	 * detection function in usb2 phy, this can help to trigger
	 * the peripheral disconnect by software.
	 */
	if (dwc->dr_mode == USB_DR_MODE_PERIPHERAL)
		phy_set_mode(dwc->usb2_generic_phy, PHY_MODE_INVALID);

	dwc->dr_mode = USB_DR_MODE_OTG;
	schedule_work(&rockchip->otg_work);
	flush_work(&rockchip->otg_work);

	/* Schedule the otg work to set the otg to new mode. */
	dwc->dr_mode = new_dr_mode;
	schedule_work(&rockchip->otg_work);
	flush_work(&rockchip->otg_work);

	/* Set phy mode */
	if (dwc->dr_mode == USB_DR_MODE_PERIPHERAL)
		phy_set_mode(dwc->usb2_generic_phy, PHY_MODE_USB_DEVICE);
	else if (dwc->dr_mode == USB_DR_MODE_HOST)
		phy_set_mode(dwc->usb2_generic_phy, PHY_MODE_USB_HOST);

	dev_info(rockchip->dev, "set new mode successfully\n");
	return count;
}

#if defined(CONFIG_USB_DWC3_HOST) || defined(CONFIG_USB_DWC3_DUAL_ROLE)
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
	int		ret, val;
	u32		reg;

	ret = readx_poll_timeout(readl, &hcd->state, val,
				 val != HC_STATE_HALT, 1000,
				 WAIT_FOR_HCD_READY_TIMEOUT);
	if (ret < 0) {
		dev_err(rockchip->dev, "Wait for HCD ready timeout\n");
		return -EINVAL;
	}

	switch (mode) {
	case TEST_J:
	case TEST_K:
	case TEST_SE0_NAK:
	case TEST_PACKET:
	case TEST_FORCE_EN:
		port_array = xhci->usb2_ports;
		reg = readl(port_array[0] + PORTPMSC);
		reg &= ~XHCI_TSTCTRL_MASK;
		reg |= mode << 28;
		writel(reg, port_array[0] + PORTPMSC);
		break;
	case USB_SS_PORT_LS_COMP_MOD:
		port_array = xhci->usb3_ports;
		xhci_set_link_state(xhci, port_array, 0, mode);
		break;
	default:
		return -EINVAL;
	}

	dev_info(rockchip->dev, "set USB HOST test mode successfully!\n");

	return 0;
}

static ssize_t host_testmode_show(struct device *device,
				  struct device_attribute *attr, char *buf)
{
	struct dwc3_rockchip	*rockchip = dev_get_drvdata(device);
	struct dwc3		*dwc = rockchip->dwc;
	struct usb_hcd		*hcd  = dev_get_drvdata(&dwc->xhci->dev);
	struct xhci_hcd		*xhci = hcd_to_xhci(hcd);
	__le32 __iomem		**port_array;
	u32			reg;
	int			ret;

	if (rockchip->dwc->dr_mode == USB_DR_MODE_PERIPHERAL) {
		dev_warn(rockchip->dev, "USB peripheral not support!\n");
		return 0;
	}

	if (hcd->state == HC_STATE_HALT) {
		dev_warn(rockchip->dev, "HOST is halted, set test mode first!\n");
		return 0;
	}

	port_array = xhci->usb2_ports;
	reg = readl(port_array[0] + PORTPMSC);
	reg &= XHCI_TSTCTRL_MASK;
	reg >>= 28;

	switch (reg) {
	case 0:
		ret = sprintf(buf, "U2: no test\n");
		break;
	case TEST_J:
		ret = sprintf(buf, "U2: test_j\n");
		break;
	case TEST_K:
		ret = sprintf(buf, "U2: test_k\n");
		break;
	case TEST_SE0_NAK:
		ret = sprintf(buf, "U2: test_se0_nak\n");
		break;
	case TEST_PACKET:
		ret = sprintf(buf, "U2: test_packet\n");
		break;
	case TEST_FORCE_EN:
		ret = sprintf(buf, "U2: test_force_enable\n");
		break;
	default:
		ret = sprintf(buf, "U2: UNKNOWN %d\n", reg);
	}

	port_array = xhci->usb3_ports;
	reg = readl(port_array[0]);
	reg &= PORT_PLS_MASK;
	if (reg == USB_SS_PORT_LS_COMP_MOD)
		ret += sprintf(buf + ret, "U3: compliance mode\n");
	else
		ret += sprintf(buf + ret, "U3: UNKNOWN %d\n", reg >> 5);

	return ret;
}

static ssize_t host_testmode_store(struct device *device,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct dwc3_rockchip		*rockchip = dev_get_drvdata(device);
	struct extcon_dev		*edev = rockchip->edev;
	u32				testmode = 0;
	bool				flip = false;
	union extcon_property_value	property;

	if (rockchip->dwc->dr_mode == USB_DR_MODE_PERIPHERAL) {
		dev_warn(rockchip->dev, "USB peripheral not support!\n");
		return count;
	}

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
	} else if (!strncmp(buf, "test_flip_u3", 12)) {
		testmode = USB_SS_PORT_LS_COMP_MOD;
		flip = true;
	} else {
		dev_warn(rockchip->dev, "Cmd not support! Try test_u3 or test_packet\n");
		return count;
	}

	if (edev && !extcon_get_cable_state_(edev, EXTCON_USB_HOST)) {
		if (extcon_get_cable_state_(edev, EXTCON_USB) > 0)
			extcon_set_cable_state_(edev, EXTCON_USB, false);

		property.intval = flip;
		extcon_set_property(edev, EXTCON_USB_HOST,
				    EXTCON_PROP_USB_TYPEC_POLARITY, property);
		extcon_set_cable_state_(edev, EXTCON_USB_HOST, true);

		/* Add a delay 1s to wait for XHCI HCD init */
		msleep(1000);

		rockchip->dwc->dr_mode = USB_DR_MODE_HOST;
	}

	dwc3_rockchip_set_test_mode(rockchip, testmode);

	return count;
}

static DEVICE_ATTR_RW(host_testmode);
#endif

static DEVICE_ATTR_RW(dwc3_mode);

static struct attribute *dwc3_rockchip_attrs[] = {
	&dev_attr_dwc3_mode.attr,
#if defined(CONFIG_USB_DWC3_HOST) || defined(CONFIG_USB_DWC3_DUAL_ROLE)
	&dev_attr_host_testmode.attr,
#endif
	NULL,
};

static struct attribute_group dwc3_rockchip_attr_group = {
	.name = NULL,	/* we want them in the same directory */
	.attrs = dwc3_rockchip_attrs,
};

static int dwc3_rockchip_device_notifier(struct notifier_block *nb,
					 unsigned long event, void *ptr)
{
	struct dwc3_rockchip *rockchip =
		container_of(nb, struct dwc3_rockchip, device_nb);

	if (!rockchip->suspended)
		schedule_work(&rockchip->otg_work);

	return NOTIFY_DONE;
}

static int dwc3_rockchip_host_notifier(struct notifier_block *nb,
				       unsigned long event, void *ptr)
{
	struct dwc3_rockchip *rockchip =
		container_of(nb, struct dwc3_rockchip, host_nb);

	if (!rockchip->suspended)
		schedule_work(&rockchip->otg_work);

	return NOTIFY_DONE;
}

static void dwc3_rockchip_otg_extcon_evt_work(struct work_struct *work)
{
	struct dwc3_rockchip	*rockchip =
		container_of(work, struct dwc3_rockchip, otg_work);
	struct dwc3		*dwc = rockchip->dwc;
	struct extcon_dev	*edev = rockchip->edev;
	struct usb_hcd		*hcd;
	struct xhci_hcd		*xhci;
	unsigned long		flags;
	int			ret;
	int			val;
	u32			reg;
	u32			count = 0;

	mutex_lock(&rockchip->lock);

	if (rockchip->force_mode ? dwc->dr_mode == USB_DR_MODE_PERIPHERAL :
	    extcon_get_cable_state_(edev, EXTCON_USB)) {
		if (rockchip->connected)
			goto out;

		/*
		 * If dr_mode is host only, never to set
		 * the mode to the peripheral mode.
		 */
		if (dwc->dr_mode == USB_DR_MODE_HOST) {
			dev_warn(rockchip->dev, "USB peripheral not support!\n");
			goto out;
		}

		/*
		 * Assert otg reset can put the dwc in P2 state, it's
		 * necessary operation prior to phy power on. However,
		 * asserting the otg reset may affect dwc chip operation.
		 * The reset will clear all of the dwc controller registers.
		 * So we need to reinit the dwc controller after deassert
		 * the reset. We use pm runtime to initialize dwc controller.
		 * Also, there are no synchronization primitives, meaning
		 * the dwc3 core code could at least in theory access chip
		 * registers while the reset is asserted, with unknown impact.
		 */
		if (!rockchip->skip_suspend) {
			reset_control_assert(rockchip->otg_rst);
			udelay(1);
			reset_control_deassert(rockchip->otg_rst);

			/* Wait until dwc3 core resume from PM suspend */
			while (dwc->dev->power.is_suspended) {
				if (++count > 1000) {
					dev_err(rockchip->dev,
						"wait for dwc3 core resume timeout!\n");
						goto out;
				}
				usleep_range(100, 200);
			}

			pm_runtime_get_sync(rockchip->dev);
			pm_runtime_get_sync(dwc->dev);
		} else {
			rockchip->skip_suspend = false;
		}

		spin_lock_irqsave(&dwc->lock, flags);
		dwc3_set_mode(dwc, DWC3_GCTL_PRTCAP_DEVICE);
		spin_unlock_irqrestore(&dwc->lock, flags);

		rockchip->connected = true;
		dev_info(rockchip->dev, "USB peripheral connected\n");
	} else if (rockchip->force_mode ? dwc->dr_mode == USB_DR_MODE_HOST :
		   extcon_get_cable_state_(edev, EXTCON_USB_HOST)) {
		if (rockchip->connected) {
			reg = dwc3_readl(dwc->regs, DWC3_GCTL);

			/*
			 * If the connected flag is true, and the DWC3 is
			 * is in device mode, it means that the Type-C Dongle
			 * is doing data role swap (UFP -> DFP), so we need
			 * to disconnect UFP first, and then swich DWC3 to
			 * DFP depends on the next extcon notifier.
			 */
			if (DWC3_GCTL_PRTCAP(reg) == DWC3_GCTL_PRTCAP_DEVICE)
				goto disconnect;
			else
				goto out;
		}

		if (rockchip->skip_suspend) {
			pm_runtime_put(dwc->dev);
			pm_runtime_put(rockchip->dev);
			rockchip->skip_suspend = false;
		}

		/*
		 * If dr_mode is device only, never to
		 * set the mode to the host mode.
		 */
		if (dwc->dr_mode == USB_DR_MODE_PERIPHERAL) {
			dev_warn(rockchip->dev, "USB HOST not support!\n");
			goto out;
		}

		/*
		 * Assert otg reset can put the dwc in P2 state, it's
		 * necessary operation prior to phy power on. However,
		 * asserting the otg reset may affect dwc chip operation.
		 * The reset will clear all of the dwc controller registers.
		 * So we need to reinit the dwc controller after deassert
		 * the reset. We use pm runtime to initialize dwc controller.
		 * Also, there are no synchronization primitives, meaning
		 * the dwc3 core code could at least in theory access chip
		 * registers while the reset is asserted, with unknown impact.
		 */
		reset_control_assert(rockchip->otg_rst);
		udelay(1);
		reset_control_deassert(rockchip->otg_rst);

		/*
		 * In usb3 phy init, it will access usb3 module, so we need
		 * to resume rockchip dev before phy init to make sure usb3
		 * pd is enabled.
		 */
		pm_runtime_get_sync(rockchip->dev);

		/*
		 * Don't abort on errors. If powering on a phy fails,
		 * we still need to init dwc controller and add the
		 * HCDs to avoid a crash when unloading the driver.
		 */
		ret = phy_power_on(dwc->usb2_generic_phy);
		if (ret < 0)
			dev_err(dwc->dev, "Failed to power on usb2 phy\n");

		ret = phy_power_on(dwc->usb3_generic_phy);
		if (ret < 0) {
			phy_power_off(dwc->usb2_generic_phy);
			dev_err(dwc->dev, "Failed to power on usb3 phy\n");
		}

		pm_runtime_get_sync(dwc->dev);

		spin_lock_irqsave(&dwc->lock, flags);
		dwc3_set_mode(dwc, DWC3_GCTL_PRTCAP_HOST);
		spin_unlock_irqrestore(&dwc->lock, flags);

		/*
		 * The following sleep helps to ensure that inserted USB3
		 * Ethernet devices are discovered if already inserted
		 * when booting.
		 */
		usleep_range(10000, 11000);

		hcd = dev_get_drvdata(&dwc->xhci->dev);

		if (hcd->state == HC_STATE_HALT) {
			usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
			usb_add_hcd(hcd->shared_hcd, hcd->irq, IRQF_SHARED);
		}

		rockchip->connected = true;
		dev_info(rockchip->dev, "USB HOST connected\n");
	} else {
		if (!rockchip->connected)
			goto out;

disconnect:
		reg = dwc3_readl(dwc->regs, DWC3_GCTL);

		/*
		 * xhci does not support runtime pm. If HCDs are not removed
		 * here and and re-added after a cable is inserted, USB3
		 * connections will not work.
		 * A clean(er) solution would be to implement runtime pm
		 * support in xhci. After that is available, this code should
		 * be removed.
		 * HCDs have to be removed here to prevent attempts by the
		 * xhci code to access xhci registers after the call to
		 * pm_runtime_put_sync_suspend(). On rk3399, this can result
		 * in a crash under certain circumstances (this was observed
		 * on 3399 chromebook if the system is running on battery).
		 */
		if (DWC3_GCTL_PRTCAP(reg) == DWC3_GCTL_PRTCAP_HOST ||
		    DWC3_GCTL_PRTCAP(reg) == DWC3_GCTL_PRTCAP_OTG) {
			hcd = dev_get_drvdata(&dwc->xhci->dev);
			xhci = hcd_to_xhci(hcd);

			if (hcd->state != HC_STATE_HALT) {
				xhci->xhc_state |= XHCI_STATE_REMOVING;

				/*
				 * Wait until XHCI controller resume from
				 * PM suspend, them we can remove hcd safely.
				 */
				while (dwc->xhci->dev.power.is_suspended) {
					if (++count > 100) {
						dev_err(rockchip->dev,
							"wait for XHCI resume 10s timeout!\n");
						goto out;
					}
					msleep(100);
				}

#ifdef CONFIG_FREEZER
				/*
				 * usb_remove_hcd() may call usb_disconnect() to
				 * remove a block device pluged in before.
				 * Unfortunately, the block layer suspend/resume
				 * path is fundamentally broken due to freezable
				 * kthreads and workqueue and may deadlock if a
				 * block device gets removed while resume is in
				 * progress.
				 *
				 * We need to add a ugly hack to avoid removing
				 * hcd and kicking off device removal while
				 * freezer is active. This is a joke but does
				 * avoid this particular deadlock when test with
				 * USB-C HUB and USB2/3 flash drive.
				 */
				while (pm_freezing)
					usleep_range(10000, 11000);
#endif

				usb_remove_hcd(hcd->shared_hcd);
				usb_remove_hcd(hcd);
			}

			phy_power_off(dwc->usb2_generic_phy);
			phy_power_off(dwc->usb3_generic_phy);
		}

		if (DWC3_GCTL_PRTCAP(reg) == DWC3_GCTL_PRTCAP_DEVICE) {
			ret = readx_poll_timeout(atomic_read,
						 &dwc->dev->power.usage_count,
						 val,
						 val < 2 && !dwc->connected,
						 1000,
						 PERIPHERAL_DISCONNECT_TIMEOUT);
			if (ret < 0) {
				rockchip->skip_suspend = true;
				dev_warn(rockchip->dev, "Peripheral disconnect timeout\n");
			}
		}

		if (!rockchip->skip_suspend) {
			pm_runtime_put_sync_suspend(dwc->dev);
			pm_runtime_put_sync_suspend(rockchip->dev);
		}

		rockchip->connected = false;
		dev_info(rockchip->dev, "USB unconnected\n");
	}

out:
	mutex_unlock(&rockchip->lock);
}

static int dwc3_rockchip_extcon_register(struct dwc3_rockchip *rockchip)
{
	int			ret;
	struct device		*dev = rockchip->dev;
	struct extcon_dev	*edev;

	if (device_property_read_bool(dev, "extcon")) {
		edev = extcon_get_edev_by_phandle(dev, 0);
		if (IS_ERR(edev)) {
			if (PTR_ERR(edev) != -EPROBE_DEFER)
				dev_err(dev, "couldn't get extcon device\n");
			return PTR_ERR(edev);
		}

		rockchip->device_nb.notifier_call =
				dwc3_rockchip_device_notifier;
		ret = extcon_register_notifier(edev, EXTCON_USB,
					       &rockchip->device_nb);
		if (ret < 0) {
			dev_err(dev, "failed to register notifier for USB\n");
			return ret;
		}

		rockchip->host_nb.notifier_call =
				dwc3_rockchip_host_notifier;
		ret = extcon_register_notifier(edev, EXTCON_USB_HOST,
					       &rockchip->host_nb);
		if (ret < 0) {
			dev_err(dev, "failed to register notifier for USB HOST\n");
			extcon_unregister_notifier(edev, EXTCON_USB,
						   &rockchip->device_nb);
			return ret;
		}

		rockchip->edev = edev;
	}

	return 0;
}

static void dwc3_rockchip_extcon_unregister(struct dwc3_rockchip *rockchip)
{
	if (!rockchip->edev)
		return;

	extcon_unregister_notifier(rockchip->edev, EXTCON_USB,
				   &rockchip->device_nb);
	extcon_unregister_notifier(rockchip->edev, EXTCON_USB_HOST,
				   &rockchip->host_nb);

	cancel_work_sync(&rockchip->otg_work);
}

static int dwc3_rockchip_probe(struct platform_device *pdev)
{
	struct dwc3_rockchip	*rockchip;
	struct device		*dev = &pdev->dev;
	struct device_node	*np = dev->of_node, *child;
	struct platform_device	*child_pdev;
	struct usb_hcd		*hcd = NULL;

	unsigned int		count;
	int			ret;
	int			i;

	rockchip = devm_kzalloc(dev, sizeof(*rockchip), GFP_KERNEL);

	if (!rockchip)
		return -ENOMEM;

	count = of_clk_get_parent_count(np);
	if (!count)
		return -ENOENT;

	rockchip->num_clocks = count;

	rockchip->clks = devm_kcalloc(dev, rockchip->num_clocks,
				      sizeof(struct clk *), GFP_KERNEL);
	if (!rockchip->clks)
		return -ENOMEM;

	platform_set_drvdata(pdev, rockchip);

	mutex_init(&rockchip->lock);

	rockchip->dev = dev;

	mutex_lock(&rockchip->lock);

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

	rockchip->otg_rst = devm_reset_control_get(dev, "usb3-otg");
	if (IS_ERR(rockchip->otg_rst)) {
		dev_err(dev, "could not get reset controller\n");
		ret = PTR_ERR(rockchip->otg_rst);
		goto err1;
	}

	child = of_get_child_by_name(np, "dwc3");
	if (!child) {
		dev_err(dev, "failed to find dwc3 core node\n");
		ret = -ENODEV;
		goto err1;
	}

	/* Allocate and initialize the core */
	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to create dwc3 core\n");
		goto err1;
	}

	INIT_WORK(&rockchip->otg_work, dwc3_rockchip_otg_extcon_evt_work);

	child_pdev = of_find_device_by_node(child);
	if (!child_pdev) {
		dev_err(dev, "failed to find dwc3 core device\n");
		ret = -ENODEV;
		goto err2;
	}

	rockchip->dwc = platform_get_drvdata(child_pdev);
	if (!rockchip->dwc) {
		dev_err(dev, "failed to get drvdata dwc3\n");
		ret = -EPROBE_DEFER;
		goto err2;
	}

	if (rockchip->dwc->dr_mode == USB_DR_MODE_HOST ||
	    rockchip->dwc->dr_mode == USB_DR_MODE_OTG) {
		hcd = dev_get_drvdata(&rockchip->dwc->xhci->dev);
		if (!hcd) {
			dev_err(dev, "fail to get drvdata hcd\n");
			ret = -EPROBE_DEFER;
			goto err2;
		}
	}

	ret = dwc3_rockchip_extcon_register(rockchip);
	if (ret < 0)
		goto err2;

	if (rockchip->edev || (rockchip->dwc->dr_mode == USB_DR_MODE_OTG)) {
		if (hcd && hcd->state != HC_STATE_HALT) {
			usb_remove_hcd(hcd->shared_hcd);
			usb_remove_hcd(hcd);
		}

		pm_runtime_set_autosuspend_delay(&child_pdev->dev,
						 DWC3_ROCKCHIP_AUTOSUSPEND_DELAY);
		pm_runtime_allow(&child_pdev->dev);
		pm_runtime_suspend(&child_pdev->dev);
		pm_runtime_put_sync(dev);

		if ((extcon_get_cable_state_(rockchip->edev,
					     EXTCON_USB) > 0) ||
		    (extcon_get_cable_state_(rockchip->edev,
					     EXTCON_USB_HOST) > 0))
			schedule_work(&rockchip->otg_work);
	} else {
		/*
		 * DWC3 work as Host only mode or Peripheral
		 * only mode, set connected flag to true, it
		 * can avoid to reset the DWC3 controller when
		 * resume from PM suspend which may cause the
		 * usb device to be reenumerated.
		 */
		 rockchip->connected = true;
	}

	ret = sysfs_create_group(&dev->kobj, &dwc3_rockchip_attr_group);
	if (ret)
		dev_err(dev, "failed to create sysfs group: %d\n", ret);

	mutex_unlock(&rockchip->lock);

	return ret;

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

	mutex_unlock(&rockchip->lock);

	return ret;
}

static int dwc3_rockchip_remove(struct platform_device *pdev)
{
	struct dwc3_rockchip	*rockchip = platform_get_drvdata(pdev);
	struct device		*dev = &pdev->dev;
	int			i;

	dwc3_rockchip_extcon_unregister(rockchip);

	sysfs_remove_group(&dev->kobj, &dwc3_rockchip_attr_group);

	/* Restore hcd state before unregistering xhci */
	if (rockchip->edev && !rockchip->connected) {
		struct usb_hcd *hcd =
			dev_get_drvdata(&rockchip->dwc->xhci->dev);

		pm_runtime_get_sync(dev);

		/*
		 * The xhci code does not expect that HCDs have been removed.
		 * It will unconditionally call usb_remove_hcd() when the xhci
		 * driver is unloaded in of_platform_depopulate(). This results
		 * in a crash if the HCDs were already removed. To avoid this
		 * crash, add the HCDs here as dummy operation.
		 * This code should be removed after pm runtime support
		 * has been added to xhci.
		 */
		if (hcd->state == HC_STATE_HALT) {
			usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
			usb_add_hcd(hcd->shared_hcd, hcd->irq, IRQF_SHARED);
		}
	}

	of_platform_depopulate(dev);

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

	device_init_wakeup(dev, false);

	return 0;
}

static int dwc3_rockchip_runtime_resume(struct device *dev)
{
	struct dwc3_rockchip	*rockchip = dev_get_drvdata(dev);
	int			i;

	for (i = 0; i < rockchip->num_clocks; i++)
		clk_enable(rockchip->clks[i]);

	device_init_wakeup(dev, true);

	return 0;
}

static int __maybe_unused dwc3_rockchip_suspend(struct device *dev)
{
	struct dwc3_rockchip *rockchip = dev_get_drvdata(dev);
	struct dwc3 *dwc = rockchip->dwc;

	rockchip->suspended = true;
	cancel_work_sync(&rockchip->otg_work);

	if (rockchip->edev && dwc->dr_mode != USB_DR_MODE_PERIPHERAL) {
		/*
		 * If USB HOST connected, we will do phy power
		 * on in extcon evt work, so need to do phy
		 * power off in suspend. And we just power off
		 * USB2 PHY here because USB3 PHY power on operation
		 * need to be done while DWC3 controller is in P2
		 * state, but after resume DWC3 controller is in
		 * P0 state. So we put USB3 PHY in power on state.
		 */
		if (extcon_get_cable_state_(rockchip->edev,
					    EXTCON_USB_HOST) > 0)
			phy_power_off(dwc->usb2_generic_phy);
	}

	return 0;
}

static int __maybe_unused dwc3_rockchip_resume(struct device *dev)
{
	struct dwc3_rockchip *rockchip = dev_get_drvdata(dev);
	struct dwc3 *dwc = rockchip->dwc;

	if (!rockchip->connected) {
		reset_control_assert(rockchip->otg_rst);
		udelay(1);
		reset_control_deassert(rockchip->otg_rst);
	}

	rockchip->suspended = false;

	if (rockchip->edev)
		schedule_work(&rockchip->otg_work);

	if (rockchip->edev && dwc->dr_mode != USB_DR_MODE_PERIPHERAL) {
		if (extcon_get_cable_state_(rockchip->edev,
					    EXTCON_USB_HOST) > 0)
			phy_power_on(dwc->usb2_generic_phy);
	}

	return 0;
}

static const struct dev_pm_ops dwc3_rockchip_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_rockchip_suspend, dwc3_rockchip_resume)
	SET_RUNTIME_PM_OPS(dwc3_rockchip_runtime_suspend,
			   dwc3_rockchip_runtime_resume, NULL)
};

#define DEV_PM_OPS      (&dwc3_rockchip_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM */

static const struct of_device_id rockchip_dwc3_match[] = {
	{ .compatible = "rockchip,rk3399-dwc3" },
	{ /* Sentinel */ }
};

MODULE_DEVICE_TABLE(of, rockchip_dwc3_match);

static struct platform_driver dwc3_rockchip_driver = {
	.probe		= dwc3_rockchip_probe,
	.remove		= dwc3_rockchip_remove,
	.driver		= {
		.name	= "rockchip-dwc3",
		.of_match_table = rockchip_dwc3_match,
		.pm	= DEV_PM_OPS,
	},
};

module_platform_driver(dwc3_rockchip_driver);

MODULE_ALIAS("platform:rockchip-dwc3");
MODULE_AUTHOR("William Wu <william.wu@rock-chips.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 ROCKCHIP Glue Layer");
