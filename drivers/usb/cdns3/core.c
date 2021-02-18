// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence USBSS and USBSSP DRD Driver.
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

#include "core.h"
#include "host-export.h"
#include "drd.h"

static int cdns_idle_init(struct cdns *cdns);

static int cdns_role_start(struct cdns *cdns, enum usb_role role)
{
	int ret;

	if (WARN_ON(role > USB_ROLE_DEVICE))
		return 0;

	mutex_lock(&cdns->mutex);
	cdns->role = role;
	mutex_unlock(&cdns->mutex);

	if (!cdns->roles[role])
		return -ENXIO;

	if (cdns->roles[role]->state == CDNS_ROLE_STATE_ACTIVE)
		return 0;

	mutex_lock(&cdns->mutex);
	ret = cdns->roles[role]->start(cdns);
	if (!ret)
		cdns->roles[role]->state = CDNS_ROLE_STATE_ACTIVE;
	mutex_unlock(&cdns->mutex);

	return ret;
}

static void cdns_role_stop(struct cdns *cdns)
{
	enum usb_role role = cdns->role;

	if (WARN_ON(role > USB_ROLE_DEVICE))
		return;

	if (cdns->roles[role]->state == CDNS_ROLE_STATE_INACTIVE)
		return;

	mutex_lock(&cdns->mutex);
	cdns->roles[role]->stop(cdns);
	cdns->roles[role]->state = CDNS_ROLE_STATE_INACTIVE;
	mutex_unlock(&cdns->mutex);
}

static void cdns_exit_roles(struct cdns *cdns)
{
	cdns_role_stop(cdns);
	cdns_drd_exit(cdns);
}

/**
 * cdns_core_init_role - initialize role of operation
 * @cdns: Pointer to cdns structure
 *
 * Returns 0 on success otherwise negative errno
 */
static int cdns_core_init_role(struct cdns *cdns)
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
		if (cdns->version == CDNSP_CONTROLLER_V2) {
			if (IS_ENABLED(CONFIG_USB_CDNSP_HOST) &&
			    IS_ENABLED(CONFIG_USB_CDNSP_GADGET))
				dr_mode = USB_DR_MODE_OTG;
			else if (IS_ENABLED(CONFIG_USB_CDNSP_HOST))
				dr_mode = USB_DR_MODE_HOST;
			else if (IS_ENABLED(CONFIG_USB_CDNSP_GADGET))
				dr_mode = USB_DR_MODE_PERIPHERAL;
		} else {
			if (IS_ENABLED(CONFIG_USB_CDNS3_HOST) &&
			    IS_ENABLED(CONFIG_USB_CDNS3_GADGET))
				dr_mode = USB_DR_MODE_OTG;
			else if (IS_ENABLED(CONFIG_USB_CDNS3_HOST))
				dr_mode = USB_DR_MODE_HOST;
			else if (IS_ENABLED(CONFIG_USB_CDNS3_GADGET))
				dr_mode = USB_DR_MODE_PERIPHERAL;
		}
	}

	/*
	 * At this point cdns->dr_mode contains strap configuration.
	 * Driver try update this setting considering kernel configuration
	 */
	best_dr_mode = cdns->dr_mode;

	ret = cdns_idle_init(cdns);
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
		if ((cdns->version == CDNSP_CONTROLLER_V2 &&
		     IS_ENABLED(CONFIG_USB_CDNSP_HOST)) ||
		    (cdns->version < CDNSP_CONTROLLER_V2 &&
		     IS_ENABLED(CONFIG_USB_CDNS3_HOST)))
			ret = cdns_host_init(cdns);
		else
			ret = -ENXIO;

		if (ret) {
			dev_err(dev, "Host initialization failed with %d\n",
				ret);
			goto err;
		}
	}

	if (dr_mode == USB_DR_MODE_OTG || dr_mode == USB_DR_MODE_PERIPHERAL) {
		if (cdns->gadget_init)
			ret = cdns->gadget_init(cdns);
		else
			ret = -ENXIO;

		if (ret) {
			dev_err(dev, "Device initialization failed with %d\n",
				ret);
			goto err;
		}
	}

	cdns->dr_mode = dr_mode;

	ret = cdns_drd_update_mode(cdns);
	if (ret)
		goto err;

	/* Initialize idle role to start with */
	ret = cdns_role_start(cdns, USB_ROLE_NONE);
	if (ret)
		goto err;

	switch (cdns->dr_mode) {
	case USB_DR_MODE_OTG:
		ret = cdns_hw_role_switch(cdns);
		if (ret)
			goto err;
		break;
	case USB_DR_MODE_PERIPHERAL:
		ret = cdns_role_start(cdns, USB_ROLE_DEVICE);
		if (ret)
			goto err;
		break;
	case USB_DR_MODE_HOST:
		ret = cdns_role_start(cdns, USB_ROLE_HOST);
		if (ret)
			goto err;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	cdns_exit_roles(cdns);
	return ret;
}

/**
 * cdns_hw_role_state_machine  - role switch state machine based on hw events.
 * @cdns: Pointer to controller structure.
 *
 * Returns next role to be entered based on hw events.
 */
static enum usb_role cdns_hw_role_state_machine(struct cdns *cdns)
{
	enum usb_role role = USB_ROLE_NONE;
	int id, vbus;

	if (cdns->dr_mode != USB_DR_MODE_OTG) {
		if (cdns_is_host(cdns))
			role = USB_ROLE_HOST;
		if (cdns_is_device(cdns))
			role = USB_ROLE_DEVICE;

		return role;
	}

	id = cdns_get_id(cdns);
	vbus = cdns_get_vbus(cdns);

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

static int cdns_idle_role_start(struct cdns *cdns)
{
	return 0;
}

static void cdns_idle_role_stop(struct cdns *cdns)
{
	/* Program Lane swap and bring PHY out of RESET */
	phy_reset(cdns->usb3_phy);
}

static int cdns_idle_init(struct cdns *cdns)
{
	struct cdns_role_driver *rdrv;

	rdrv = devm_kzalloc(cdns->dev, sizeof(*rdrv), GFP_KERNEL);
	if (!rdrv)
		return -ENOMEM;

	rdrv->start = cdns_idle_role_start;
	rdrv->stop = cdns_idle_role_stop;
	rdrv->state = CDNS_ROLE_STATE_INACTIVE;
	rdrv->suspend = NULL;
	rdrv->resume = NULL;
	rdrv->name = "idle";

	cdns->roles[USB_ROLE_NONE] = rdrv;

	return 0;
}

/**
 * cdns_hw_role_switch - switch roles based on HW state
 * @cdns: controller
 */
int cdns_hw_role_switch(struct cdns *cdns)
{
	enum usb_role real_role, current_role;
	int ret = 0;

	/* Depends on role switch class */
	if (cdns->role_sw)
		return 0;

	pm_runtime_get_sync(cdns->dev);

	current_role = cdns->role;
	real_role = cdns_hw_role_state_machine(cdns);

	/* Do nothing if nothing changed */
	if (current_role == real_role)
		goto exit;

	cdns_role_stop(cdns);

	dev_dbg(cdns->dev, "Switching role %d -> %d", current_role, real_role);

	ret = cdns_role_start(cdns, real_role);
	if (ret) {
		/* Back to current role */
		dev_err(cdns->dev, "set %d has failed, back to %d\n",
			real_role, current_role);
		ret = cdns_role_start(cdns, current_role);
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
static enum usb_role cdns_role_get(struct usb_role_switch *sw)
{
	struct cdns *cdns = usb_role_switch_get_drvdata(sw);

	return cdns->role;
}

/**
 * cdns_role_set - set current role of controller.
 *
 * @sw: pointer to USB role switch structure
 * @role: the previous role
 * Handles below events:
 * - Role switch for dual-role devices
 * - USB_ROLE_GADGET <--> USB_ROLE_NONE for peripheral-only devices
 */
static int cdns_role_set(struct usb_role_switch *sw, enum usb_role role)
{
	struct cdns *cdns = usb_role_switch_get_drvdata(sw);
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

	cdns_role_stop(cdns);
	ret = cdns_role_start(cdns, role);
	if (ret)
		dev_err(cdns->dev, "set role %d has failed\n", role);

pm_put:
	pm_runtime_put_sync(cdns->dev);
	return ret;
}


/**
 * cdns_wakeup_irq - interrupt handler for wakeup events
 * @irq: irq number for cdns3/cdnsp core device
 * @data: structure of cdns
 *
 * Returns IRQ_HANDLED or IRQ_NONE
 */
static irqreturn_t cdns_wakeup_irq(int irq, void *data)
{
	struct cdns *cdns = data;

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
 * cdns_probe - probe for cdns3/cdnsp core device
 * @cdns: Pointer to cdns structure.
 *
 * Returns 0 on success otherwise negative errno
 */
int cdns_init(struct cdns *cdns)
{
	struct device *dev = cdns->dev;
	int ret;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "error setting dma mask: %d\n", ret);
		return ret;
	}

	mutex_init(&cdns->mutex);

	if (device_property_read_bool(dev, "usb-role-switch")) {
		struct usb_role_switch_desc sw_desc = { };

		sw_desc.set = cdns_role_set;
		sw_desc.get = cdns_role_get;
		sw_desc.allow_userspace_control = true;
		sw_desc.driver_data = cdns;
		sw_desc.fwnode = dev->fwnode;

		cdns->role_sw = usb_role_switch_register(dev, &sw_desc);
		if (IS_ERR(cdns->role_sw)) {
			dev_warn(dev, "Unable to register Role Switch\n");
			return PTR_ERR(cdns->role_sw);
		}
	}

	if (cdns->wakeup_irq) {
		ret = devm_request_irq(cdns->dev, cdns->wakeup_irq,
						cdns_wakeup_irq,
						IRQF_SHARED,
						dev_name(cdns->dev), cdns);

		if (ret) {
			dev_err(cdns->dev, "couldn't register wakeup irq handler\n");
			goto role_switch_unregister;
		}
	}

	ret = cdns_drd_init(cdns);
	if (ret)
		goto init_failed;

	ret = cdns_core_init_role(cdns);
	if (ret)
		goto init_failed;

	spin_lock_init(&cdns->lock);

	dev_dbg(dev, "Cadence USB3 core: probe succeed\n");

	return 0;
init_failed:
	cdns_drd_exit(cdns);
role_switch_unregister:
	if (cdns->role_sw)
		usb_role_switch_unregister(cdns->role_sw);

	return ret;
}
EXPORT_SYMBOL_GPL(cdns_init);

/**
 * cdns_remove - unbind drd driver and clean up
 * @cdns: Pointer to cdns structure.
 *
 * Returns 0 on success otherwise negative errno
 */
int cdns_remove(struct cdns *cdns)
{
	cdns_exit_roles(cdns);
	usb_role_switch_unregister(cdns->role_sw);

	return 0;
}
EXPORT_SYMBOL_GPL(cdns_remove);

#ifdef CONFIG_PM_SLEEP
int cdns_suspend(struct cdns *cdns)
{
	struct device *dev = cdns->dev;
	unsigned long flags;

	if (pm_runtime_status_suspended(dev))
		pm_runtime_resume(dev);

	if (cdns->roles[cdns->role]->suspend) {
		spin_lock_irqsave(&cdns->lock, flags);
		cdns->roles[cdns->role]->suspend(cdns, false);
		spin_unlock_irqrestore(&cdns->lock, flags);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cdns_suspend);

int cdns_resume(struct cdns *cdns, u8 set_active)
{
	struct device *dev = cdns->dev;
	enum usb_role real_role;
	bool role_changed = false;
	int ret;

	if (cdns_power_is_lost(cdns)) {
		if (cdns->role_sw) {
			cdns->role = cdns_role_get(cdns->role_sw);
		} else {
			real_role = cdns_hw_role_state_machine(cdns);
			if (real_role != cdns->role) {
				ret = cdns_hw_role_switch(cdns);
				if (ret)
					return ret;
				role_changed = true;
			}
		}

		if (!role_changed) {
			if (cdns->role == USB_ROLE_HOST)
				ret = cdns_drd_host_on(cdns);
			else if (cdns->role == USB_ROLE_DEVICE)
				ret = cdns_drd_gadget_on(cdns);

			if (ret)
				return ret;
		}
	}

	if (cdns->roles[cdns->role]->resume)
		cdns->roles[cdns->role]->resume(cdns, cdns_power_is_lost(cdns));

	if (set_active) {
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cdns_resume);
#endif /* CONFIG_PM_SLEEP */

MODULE_AUTHOR("Peter Chen <peter.chen@nxp.com>");
MODULE_AUTHOR("Pawel Laszczak <pawell@cadence.com>");
MODULE_AUTHOR("Roger Quadros <rogerq@ti.com>");
MODULE_DESCRIPTION("Cadence USBSS and USBSSP DRD Driver");
MODULE_LICENSE("GPL");
