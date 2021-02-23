// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence USBSS DRD Driver.
 *
 * Copyright (C) 2018-2019 Cadence.
 * Copyright (C) 2017-2018 NXP
 * Copyright (C) 2019 Texas Instruments
 *
 * Author: Peter Chen <peter.chen@nxp.com>
 *         Pawel Laszczak <pawell@cadence.com>
 *         Roger Quadros <rogerq@ti.com>
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>

#include "gadget.h"
#include "core.h"
#include "host-export.h"
#include "gadget-export.h"
#include "drd.h"

static int cdns3_idle_init(struct cdns3 *cdns);

static int cdns3_role_start(struct cdns3 *cdns, enum usb_role role)
{
	int ret;

	if (WARN_ON(role > USB_ROLE_DEVICE))
		return 0;

	mutex_lock(&cdns->mutex);
	cdns->role = role;
	mutex_unlock(&cdns->mutex);

	if (!cdns->roles[role])
		return -ENXIO;

	if (cdns->roles[role]->state == CDNS3_ROLE_STATE_ACTIVE)
		return 0;

	mutex_lock(&cdns->mutex);
	ret = cdns->roles[role]->start(cdns);
	if (!ret)
		cdns->roles[role]->state = CDNS3_ROLE_STATE_ACTIVE;
	mutex_unlock(&cdns->mutex);

	return ret;
}

static void cdns3_role_stop(struct cdns3 *cdns)
{
	enum usb_role role = cdns->role;

	if (WARN_ON(role > USB_ROLE_DEVICE))
		return;

	if (cdns->roles[role]->state == CDNS3_ROLE_STATE_INACTIVE)
		return;

	mutex_lock(&cdns->mutex);
	cdns->roles[role]->stop(cdns);
	cdns->roles[role]->state = CDNS3_ROLE_STATE_INACTIVE;
	mutex_unlock(&cdns->mutex);
}

static void cdns3_exit_roles(struct cdns3 *cdns)
{
	cdns3_role_stop(cdns);
	cdns3_drd_exit(cdns);
}

/**
 * cdns3_core_init_role - initialize role of operation
 * @cdns: Pointer to cdns3 structure
 *
 * Returns 0 on success otherwise negative errno
 */
static int cdns3_core_init_role(struct cdns3 *cdns)
{
	struct device *dev = cdns->dev;
	enum usb_dr_mode best_dr_mode;
	enum usb_dr_mode dr_mode;
	int ret;

	dr_mode = usb_get_dr_mode(dev);
	cdns->role = USB_ROLE_NONE;

	/*
	 * If driver can't read mode by means of usb_get_dr_mode function then
	 * chooses mode according with Kernel configuration. This setting
	 * can be restricted later depending on strap pin configuration.
	 */
	if (dr_mode == USB_DR_MODE_UNKNOWN) {
		if (IS_ENABLED(CONFIG_USB_CDNS3_HOST) &&
		    IS_ENABLED(CONFIG_USB_CDNS3_GADGET))
			dr_mode = USB_DR_MODE_OTG;
		else if (IS_ENABLED(CONFIG_USB_CDNS3_HOST))
			dr_mode = USB_DR_MODE_HOST;
		else if (IS_ENABLED(CONFIG_USB_CDNS3_GADGET))
			dr_mode = USB_DR_MODE_PERIPHERAL;
	}

	/*
	 * At this point cdns->dr_mode contains strap configuration.
	 * Driver try update this setting considering kernel configuration
	 */
	best_dr_mode = cdns->dr_mode;

	ret = cdns3_idle_init(cdns);
	if (ret)
		return ret;

	if (dr_mode == USB_DR_MODE_OTG) {
		best_dr_mode = cdns->dr_mode;
	} else if (cdns->dr_mode == USB_DR_MODE_OTG) {
		best_dr_mode = dr_mode;
	} else if (cdns->dr_mode != dr_mode) {
		dev_err(dev, "Incorrect DRD configuration\n");
		return -EINVAL;
	}

	dr_mode = best_dr_mode;

	if (dr_mode == USB_DR_MODE_OTG || dr_mode == USB_DR_MODE_HOST) {
		ret = cdns3_host_init(cdns);
		if (ret) {
			dev_err(dev, "Host initialization failed with %d\n",
				ret);
			goto err;
		}
	}

	if (dr_mode == USB_DR_MODE_OTG || dr_mode == USB_DR_MODE_PERIPHERAL) {
		ret = cdns3_gadget_init(cdns);
		if (ret) {
			dev_err(dev, "Device initialization failed with %d\n",
				ret);
			goto err;
		}
	}

	cdns->dr_mode = dr_mode;

	ret = cdns3_drd_update_mode(cdns);
	if (ret)
		goto err;

	/* Initialize idle role to start with */
	ret = cdns3_role_start(cdns, USB_ROLE_NONE);
	if (ret)
		goto err;

	switch (cdns->dr_mode) {
	case USB_DR_MODE_OTG:
		ret = cdns3_hw_role_switch(cdns);
		if (ret)
			goto err;
		break;
	case USB_DR_MODE_PERIPHERAL:
		ret = cdns3_role_start(cdns, USB_ROLE_DEVICE);
		if (ret)
			goto err;
		break;
	case USB_DR_MODE_HOST:
		ret = cdns3_role_start(cdns, USB_ROLE_HOST);
		if (ret)
			goto err;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	cdns3_exit_roles(cdns);
	return ret;
}

/**
 * cdns3_hw_role_state_machine  - role switch state machine based on hw events.
 * @cdns: Pointer to controller structure.
 *
 * Returns next role to be entered based on hw events.
 */
static enum usb_role cdns3_hw_role_state_machine(struct cdns3 *cdns)
{
	enum usb_role role = USB_ROLE_NONE;
	int id, vbus;

	if (cdns->dr_mode != USB_DR_MODE_OTG) {
		if (cdns3_is_host(cdns))
			role = USB_ROLE_HOST;
		if (cdns3_is_device(cdns))
			role = USB_ROLE_DEVICE;

		return role;
	}

	id = cdns3_get_id(cdns);
	vbus = cdns3_get_vbus(cdns);

	/*
	 * Role change state machine
	 * Inputs: ID, VBUS
	 * Previous state: cdns->role
	 * Next state: role
	 */
	role = cdns->role;

	switch (role) {
	case USB_ROLE_NONE:
		/*
		 * Driver treats USB_ROLE_NONE synonymous to IDLE state from
		 * controller specification.
		 */
		if (!id)
			role = USB_ROLE_HOST;
		else if (vbus)
			role = USB_ROLE_DEVICE;
		break;
	case USB_ROLE_HOST: /* from HOST, we can only change to NONE */
		if (id)
			role = USB_ROLE_NONE;
		break;
	case USB_ROLE_DEVICE: /* from GADGET, we can only change to NONE*/
		if (!vbus)
			role = USB_ROLE_NONE;
		break;
	}

	dev_dbg(cdns->dev, "role %d -> %d\n", cdns->role, role);

	return role;
}

static int cdns3_idle_role_start(struct cdns3 *cdns)
{
	return 0;
}

static void cdns3_idle_role_stop(struct cdns3 *cdns)
{
	/* Program Lane swap and bring PHY out of RESET */
	phy_reset(cdns->usb3_phy);
}

static int cdns3_idle_init(struct cdns3 *cdns)
{
	struct cdns3_role_driver *rdrv;

	rdrv = devm_kzalloc(cdns->dev, sizeof(*rdrv), GFP_KERNEL);
	if (!rdrv)
		return -ENOMEM;

	rdrv->start = cdns3_idle_role_start;
	rdrv->stop = cdns3_idle_role_stop;
	rdrv->state = CDNS3_ROLE_STATE_INACTIVE;
	rdrv->suspend = NULL;
	rdrv->resume = NULL;
	rdrv->name = "idle";

	cdns->roles[USB_ROLE_NONE] = rdrv;

	return 0;
}

/**
 * cdns3_hw_role_switch - switch roles based on HW state
 * @cdns: controller
 */
int cdns3_hw_role_switch(struct cdns3 *cdns)
{
	enum usb_role real_role, current_role;
	int ret = 0;

	/* Depends on role switch class */
	if (cdns->role_sw)
		return 0;

	pm_runtime_get_sync(cdns->dev);

	current_role = cdns->role;
	real_role = cdns3_hw_role_state_machine(cdns);

	/* Do nothing if nothing changed */
	if (current_role == real_role)
		goto exit;

	cdns3_role_stop(cdns);

	dev_dbg(cdns->dev, "Switching role %d -> %d", current_role, real_role);

	ret = cdns3_role_start(cdns, real_role);
	if (ret) {
		/* Back to current role */
		dev_err(cdns->dev, "set %d has failed, back to %d\n",
			real_role, current_role);
		ret = cdns3_role_start(cdns, current_role);
		if (ret)
			dev_err(cdns->dev, "back to %d failed too\n",
				current_role);
	}
exit:
	pm_runtime_put_sync(cdns->dev);
	return ret;
}

/**
 * cdsn3_role_get - get current role of controller.
 *
 * @sw: pointer to USB role switch structure
 *
 * Returns role
 */
static enum usb_role cdns3_role_get(struct usb_role_switch *sw)
{
	struct cdns3 *cdns = usb_role_switch_get_drvdata(sw);

	return cdns->role;
}

/**
 * cdns3_role_set - set current role of controller.
 *
 * @sw: pointer to USB role switch structure
 * @role: the previous role
 * Handles below events:
 * - Role switch for dual-role devices
 * - USB_ROLE_GADGET <--> USB_ROLE_NONE for peripheral-only devices
 */
static int cdns3_role_set(struct usb_role_switch *sw, enum usb_role role)
{
	struct cdns3 *cdns = usb_role_switch_get_drvdata(sw);
	int ret = 0;

	pm_runtime_get_sync(cdns->dev);

	if (cdns->role == role)
		goto pm_put;

	if (cdns->dr_mode == USB_DR_MODE_HOST) {
		switch (role) {
		case USB_ROLE_NONE:
		case USB_ROLE_HOST:
			break;
		default:
			goto pm_put;
		}
	}

	if (cdns->dr_mode == USB_DR_MODE_PERIPHERAL) {
		switch (role) {
		case USB_ROLE_NONE:
		case USB_ROLE_DEVICE:
			break;
		default:
			goto pm_put;
		}
	}

	cdns3_role_stop(cdns);
	ret = cdns3_role_start(cdns, role);
	if (ret)
		dev_err(cdns->dev, "set role %d has failed\n", role);

pm_put:
	pm_runtime_put_sync(cdns->dev);
	return ret;
}

static int set_phy_power_on(struct cdns3 *cdns)
{
	int ret;

	ret = phy_power_on(cdns->usb2_phy);
	if (ret)
		return ret;

	ret = phy_power_on(cdns->usb3_phy);
	if (ret)
		phy_power_off(cdns->usb2_phy);

	return ret;
}

static void set_phy_power_off(struct cdns3 *cdns)
{
	phy_power_off(cdns->usb3_phy);
	phy_power_off(cdns->usb2_phy);
}

/**
 * cdns3_wakeup_irq - interrupt handler for wakeup events
 * @irq: irq number for cdns3 core device
 * @data: structure of cdns3
 *
 * Returns IRQ_HANDLED or IRQ_NONE
 */
static irqreturn_t cdns3_wakeup_irq(int irq, void *data)
{
	struct cdns3 *cdns = data;

	if (cdns->in_lpm) {
		disable_irq_nosync(irq);
		cdns->wakeup_pending = true;
		if ((cdns->role == USB_ROLE_HOST) && cdns->host_dev)
			pm_request_resume(&cdns->host_dev->dev);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/**
 * cdns3_probe - probe for cdns3 core device
 * @pdev: Pointer to cdns3 core platform device
 *
 * Returns 0 on success otherwise negative errno
 */
static int cdns3_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource	*res;
	struct cdns3 *cdns;
	void __iomem *regs;
	int ret;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "error setting dma mask: %d\n", ret);
		return ret;
	}

	cdns = devm_kzalloc(dev, sizeof(*cdns), GFP_KERNEL);
	if (!cdns)
		return -ENOMEM;

	cdns->dev = dev;
	cdns->pdata = dev_get_platdata(dev);

	platform_set_drvdata(pdev, cdns);

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "host");
	if (!res) {
		dev_err(dev, "missing host IRQ\n");
		return -ENODEV;
	}

	cdns->xhci_res[0] = *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "xhci");
	if (!res) {
		dev_err(dev, "couldn't get xhci resource\n");
		return -ENXIO;
	}

	cdns->xhci_res[1] = *res;

	cdns->dev_irq = platform_get_irq_byname(pdev, "peripheral");
	if (cdns->dev_irq < 0)
		return cdns->dev_irq;

	regs = devm_platform_ioremap_resource_byname(pdev, "dev");
	if (IS_ERR(regs))
		return PTR_ERR(regs);
	cdns->dev_regs	= regs;

	cdns->otg_irq = platform_get_irq_byname(pdev, "otg");
	if (cdns->otg_irq < 0)
		return cdns->otg_irq;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "otg");
	if (!res) {
		dev_err(dev, "couldn't get otg resource\n");
		return -ENXIO;
	}

	cdns->phyrst_a_enable = device_property_read_bool(dev, "cdns,phyrst-a-enable");

	cdns->otg_res = *res;

	cdns->wakeup_irq = platform_get_irq_byname_optional(pdev, "wakeup");
	if (cdns->wakeup_irq == -EPROBE_DEFER)
		return cdns->wakeup_irq;
	else if (cdns->wakeup_irq == 0)
		return -EINVAL;

	if (cdns->wakeup_irq < 0) {
		dev_dbg(dev, "couldn't get wakeup irq\n");
		cdns->wakeup_irq = 0x0;
	}

	mutex_init(&cdns->mutex);

	cdns->usb2_phy = devm_phy_optional_get(dev, "cdns3,usb2-phy");
	if (IS_ERR(cdns->usb2_phy))
		return PTR_ERR(cdns->usb2_phy);

	ret = phy_init(cdns->usb2_phy);
	if (ret)
		return ret;

	cdns->usb3_phy = devm_phy_optional_get(dev, "cdns3,usb3-phy");
	if (IS_ERR(cdns->usb3_phy))
		return PTR_ERR(cdns->usb3_phy);

	ret = phy_init(cdns->usb3_phy);
	if (ret)
		goto err1;

	ret = set_phy_power_on(cdns);
	if (ret)
		goto err2;

	if (device_property_read_bool(dev, "usb-role-switch")) {
		struct usb_role_switch_desc sw_desc = { };

		sw_desc.set = cdns3_role_set;
		sw_desc.get = cdns3_role_get;
		sw_desc.allow_userspace_control = true;
		sw_desc.driver_data = cdns;
		sw_desc.fwnode = dev->fwnode;

		cdns->role_sw = usb_role_switch_register(dev, &sw_desc);
		if (IS_ERR(cdns->role_sw)) {
			ret = PTR_ERR(cdns->role_sw);
			dev_warn(dev, "Unable to register Role Switch\n");
			goto err3;
		}
	}

	if (cdns->wakeup_irq) {
		ret = devm_request_irq(cdns->dev, cdns->wakeup_irq,
						cdns3_wakeup_irq,
						IRQF_SHARED,
						dev_name(cdns->dev), cdns);

		if (ret) {
			dev_err(cdns->dev, "couldn't register wakeup irq handler\n");
			goto err4;
		}
	}

	ret = cdns3_drd_init(cdns);
	if (ret)
		goto err4;

	ret = cdns3_core_init_role(cdns);
	if (ret)
		goto err4;

	spin_lock_init(&cdns->lock);
	device_set_wakeup_capable(dev, true);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	if (!(cdns->pdata && (cdns->pdata->quirks & CDNS3_DEFAULT_PM_RUNTIME_ALLOW)))
		pm_runtime_forbid(dev);

	/*
	 * The controller needs less time between bus and controller suspend,
	 * and we also needs a small delay to avoid frequently entering low
	 * power mode.
	 */
	pm_runtime_set_autosuspend_delay(dev, 20);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_use_autosuspend(dev);
	dev_dbg(dev, "Cadence USB3 core: probe succeed\n");

	return 0;
err4:
	cdns3_drd_exit(cdns);
	if (cdns->role_sw)
		usb_role_switch_unregister(cdns->role_sw);
err3:
	set_phy_power_off(cdns);
err2:
	phy_exit(cdns->usb3_phy);
err1:
	phy_exit(cdns->usb2_phy);

	return ret;
}

/**
 * cdns3_remove - unbind drd driver and clean up
 * @pdev: Pointer to Linux platform device
 *
 * Returns 0 on success otherwise negative errno
 */
static int cdns3_remove(struct platform_device *pdev)
{
	struct cdns3 *cdns = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	cdns3_exit_roles(cdns);
	usb_role_switch_unregister(cdns->role_sw);
	set_phy_power_off(cdns);
	phy_exit(cdns->usb2_phy);
	phy_exit(cdns->usb3_phy);
	return 0;
}

#ifdef CONFIG_PM

static int cdns3_set_platform_suspend(struct device *dev,
		bool suspend, bool wakeup)
{
	struct cdns3 *cdns = dev_get_drvdata(dev);
	int ret = 0;

	if (cdns->pdata && cdns->pdata->platform_suspend)
		ret = cdns->pdata->platform_suspend(dev, suspend, wakeup);

	return ret;
}

static int cdns3_controller_suspend(struct device *dev, pm_message_t msg)
{
	struct cdns3 *cdns = dev_get_drvdata(dev);
	bool wakeup;
	unsigned long flags;

	if (cdns->in_lpm)
		return 0;

	if (PMSG_IS_AUTO(msg))
		wakeup = true;
	else
		wakeup = device_may_wakeup(dev);

	cdns3_set_platform_suspend(cdns->dev, true, wakeup);
	set_phy_power_off(cdns);
	spin_lock_irqsave(&cdns->lock, flags);
	cdns->in_lpm = true;
	spin_unlock_irqrestore(&cdns->lock, flags);
	dev_dbg(cdns->dev, "%s ends\n", __func__);

	return 0;
}

static int cdns3_controller_resume(struct device *dev, pm_message_t msg)
{
	struct cdns3 *cdns = dev_get_drvdata(dev);
	int ret;
	unsigned long flags;

	if (!cdns->in_lpm)
		return 0;

	ret = set_phy_power_on(cdns);
	if (ret)
		return ret;

	cdns3_set_platform_suspend(cdns->dev, false, false);

	spin_lock_irqsave(&cdns->lock, flags);
	if (cdns->roles[cdns->role]->resume && !PMSG_IS_AUTO(msg))
		cdns->roles[cdns->role]->resume(cdns, false);

	cdns->in_lpm = false;
	spin_unlock_irqrestore(&cdns->lock, flags);
	if (cdns->wakeup_pending) {
		cdns->wakeup_pending = false;
		enable_irq(cdns->wakeup_irq);
	}
	dev_dbg(cdns->dev, "%s ends\n", __func__);

	return ret;
}

static int cdns3_runtime_suspend(struct device *dev)
{
	return cdns3_controller_suspend(dev, PMSG_AUTO_SUSPEND);
}

static int cdns3_runtime_resume(struct device *dev)
{
	return cdns3_controller_resume(dev, PMSG_AUTO_RESUME);
}
#ifdef CONFIG_PM_SLEEP

static int cdns3_suspend(struct device *dev)
{
	struct cdns3 *cdns = dev_get_drvdata(dev);
	unsigned long flags;

	if (pm_runtime_status_suspended(dev))
		pm_runtime_resume(dev);

	if (cdns->roles[cdns->role]->suspend) {
		spin_lock_irqsave(&cdns->lock, flags);
		cdns->roles[cdns->role]->suspend(cdns, false);
		spin_unlock_irqrestore(&cdns->lock, flags);
	}

	return cdns3_controller_suspend(dev, PMSG_SUSPEND);
}

static int cdns3_resume(struct device *dev)
{
	int ret;

	ret = cdns3_controller_resume(dev, PMSG_RESUME);
	if (ret)
		return ret;

	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return ret;
}
#endif /* CONFIG_PM_SLEEP */
#endif /* CONFIG_PM */

static const struct dev_pm_ops cdns3_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cdns3_suspend, cdns3_resume)
	SET_RUNTIME_PM_OPS(cdns3_runtime_suspend, cdns3_runtime_resume, NULL)
};

#ifdef CONFIG_OF
static const struct of_device_id of_cdns3_match[] = {
	{ .compatible = "cdns,usb3" },
	{ },
};
MODULE_DEVICE_TABLE(of, of_cdns3_match);
#endif

static struct platform_driver cdns3_driver = {
	.probe		= cdns3_probe,
	.remove		= cdns3_remove,
	.driver		= {
		.name	= "cdns-usb3",
		.of_match_table	= of_match_ptr(of_cdns3_match),
		.pm	= &cdns3_pm_ops,
	},
};

module_platform_driver(cdns3_driver);

MODULE_ALIAS("platform:cdns3");
MODULE_AUTHOR("Pawel Laszczak <pawell@cadence.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Cadence USB3 DRD Controller Driver");
