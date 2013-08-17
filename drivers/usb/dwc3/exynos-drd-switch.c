/**
 * exynos-drd-switch.c - Exynos SuperSpeed USB 3.0 DRD role switch
 *
 * This driver implements the otg final state machine and controls the
 * activation of the device controller or host controller. The ID pin
 * GPIO interrupt is used for this purpose. The VBus GPIO is used to
 * detect valid B-Session in B-Device mode.
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Anton Tikhomirov <av.tikhomirov@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/usb/hcd.h>
#include <linux/usb/gadget.h>
#include <linux/usb/exynos_usb3_drd.h>
#include <linux/platform_data/dwc3-exynos.h>

#include "exynos-drd.h"
#include "exynos-drd-switch.h"
#include "xhci.h"

static inline enum id_pin_state exynos_drd_switch_sanitize_id(int state)
{
	enum id_pin_state id_state;

	if (state < 0)
		id_state = NA;
	else
		id_state = state ? B_DEV : A_DEV;

	return id_state;
}

/**
 * exynos_drd_switch_get_id_state - get connector ID state.
 *
 * @drd_switch: Pointer to the DRD switch structure.
 *
 * Returns ID pin state.
 */
static enum id_pin_state
exynos_drd_switch_get_id_state(struct exynos_drd_switch *drd_switch)
{
	struct exynos_drd *drd = container_of(drd_switch->core,
						struct exynos_drd, core);
	struct platform_device *pdev = to_platform_device(drd->dev);
	struct dwc3_exynos_data *pdata = drd->pdata;
	int state;
	enum id_pin_state id_state;

	if (pdata->get_id_state)
		state = pdata->get_id_state(pdev);
	else
		state = -1;

	id_state = exynos_drd_switch_sanitize_id(state);

	return id_state;
}

/**
 * exynos_drd_switch_get_bses_vld - get B-Session status.
 *
 * @drd_switch: Pointer to the DRD switch structure.
 *
 * Returns true if B-Session is valid, false otherwise.
 */
static bool exynos_drd_switch_get_bses_vld(struct exynos_drd_switch *drd_switch)
{
	struct exynos_drd *drd = container_of(drd_switch->core,
						struct exynos_drd, core);
	struct platform_device *pdev = to_platform_device(drd->dev);
	struct dwc3_exynos_data *pdata = drd->pdata;
	bool valid;

	if (pdata->get_bses_vld)
		valid = pdata->get_bses_vld(pdev);
	else
		/* If VBus sensing is not available we always return true */
		valid = true;

	return valid;
}

/**
 * exynos_drd_switch_ases_vbus_ctrl - turn on VBus switch in A-Device mode.
 *
 * @drd_switch: Pointer to the DRD switch structure.
 * @on: turn on / turn off VBus switch.
 */
static void
exynos_drd_switch_ases_vbus_ctrl(struct exynos_drd_switch *drd_switch, int on)
{
	struct exynos_drd *drd = container_of(drd_switch->core,
						struct exynos_drd, core);
	struct platform_device *pdev = to_platform_device(drd->dev);
	struct dwc3_exynos_data *pdata = drd->pdata;

	if (pdata->vbus_ctrl)
		pdata->vbus_ctrl(pdev, on);
}

/**
 * exynos_drd_switch_schedule_work - schedule OTG state machine work with delay.
 *
 * @work: Work to schedule.
 * @msec: Delay in milliseconds.
 *
 * Prevents state machine running if ID state is N/A. Use this function
 * to schedule work.
 */
static void exynos_drd_switch_schedule_dwork(struct delayed_work *work,
					     unsigned int msec)
{
	struct exynos_drd_switch *drd_switch;

	if (work) {
		drd_switch = container_of(work, struct exynos_drd_switch, work);

		if (drd_switch->id_state != NA)
			queue_delayed_work(drd_switch->wq, work,
					   msecs_to_jiffies(msec));
	}
}

static inline void exynos_drd_switch_schedule_work(struct delayed_work *work)
{
	exynos_drd_switch_schedule_dwork(work, 0);
}

/**
 * exynos_drd_switch_is_host_off - check host's PM status.
 *
 * @otg: Pointer to the usb_otg structure.
 *
 * Before peripheral can start two conditions must be met:
 * 1. Host's completely resumed after system sleep.
 * 2. Host is runtime suspended.
 */
static bool exynos_drd_switch_is_host_off(struct usb_otg *otg)
{
	struct usb_hcd *hcd;
	struct device *dev;

	if (!otg->host)
		/* REVISIT: what should we return here? */
		return true;

	hcd = bus_to_hcd(otg->host);
	dev = hcd->self.controller;

	return pm_runtime_suspended(dev);
}

/**
 * exynos_drd_switch_start_host -  helper function for starting/stoping the host
 * controller driver.
 *
 * @otg: Pointer to the usb_otg structure.
 * @on: start / stop the host controller driver.
 *
 * Returns 0 on success otherwise negative errno.
 */
static int exynos_drd_switch_start_host(struct usb_otg *otg, int on)
{
	struct exynos_drd_switch *drd_switch = container_of(otg,
					struct exynos_drd_switch, otg);
	struct usb_hcd *hcd;
	struct xhci_hcd *xhci;
	struct device *xhci_dev;
	int ret = 0;

	if (!otg->host)
		return -EINVAL;

	dev_dbg(otg->phy->dev, "Turn %s host %s\n",
			on ? "on" : "off", otg->host->bus_name);

	hcd = bus_to_hcd(otg->host);
	xhci = hcd_to_xhci(hcd);
	xhci_dev = hcd->self.controller;

	if (on) {
		wake_lock(&drd_switch->wakelock);
		/*
		 * Clear runtime_error flag. The flag could be
		 * set when user space accessed the host while DRD
		 * was in B-Dev mode.
		 */
		pm_runtime_disable(xhci_dev);
		if (pm_runtime_status_suspended(xhci_dev))
			pm_runtime_set_suspended(xhci_dev);
		else
			pm_runtime_set_active(xhci_dev);
		pm_runtime_enable(xhci_dev);

		ret = pm_runtime_get_sync(xhci_dev);
		if (ret < 0 && ret != -EINPROGRESS) {
			pm_runtime_put_noidle(xhci_dev);
			goto err;
		}

		exynos_drd_switch_ases_vbus_ctrl(drd_switch, 1);
	} else {
		exynos_drd_switch_ases_vbus_ctrl(drd_switch, 0);

		ret = pm_runtime_put_sync(xhci_dev);
		if (ret == -EAGAIN)
			pm_runtime_get_noresume(xhci_dev);
		else
			wake_unlock(&drd_switch->wakelock);
	}

err:
	/* ret can be 1 after pm_runtime_get_sync */
	return (ret < 0) ? ret : 0;
}

/**
 * exynos_drd_switch_set_host -  bind/unbind the host controller driver.
 *
 * @otg: Pointer to the usb_otg structure.
 * @host: Pointer to the usb_bus structure.
 *
 * Returns 0 on success otherwise negative errno.
 */
static int exynos_drd_switch_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	struct exynos_drd_switch *drd_switch = container_of(otg,
					struct exynos_drd_switch, otg);
	bool activate = false;
	unsigned long flags;

	spin_lock_irqsave(&drd_switch->lock, flags);

	if (host) {
		dev_dbg(otg->phy->dev, "Binding host %s\n", host->bus_name);
		otg->host = host;

		/*
		 * Prevents unnecessary activation of the work function.
		 * If both peripheral and host are set or if ID pin is low
		 * then we ensure that work function will enter to valid state.
		 */
		if (otg->gadget || drd_switch->id_state == A_DEV)
			activate = true;
	} else {
		dev_dbg(otg->phy->dev, "Unbinding host\n");

		if (otg->phy->state == OTG_STATE_A_HOST) {
			exynos_drd_switch_start_host(otg, 0);
			otg->host = NULL;
			otg->phy->state = OTG_STATE_UNDEFINED;
			activate = true;
		} else {
			otg->host = NULL;
		}
	}

	spin_unlock_irqrestore(&drd_switch->lock, flags);

	if (activate)
		exynos_drd_switch_schedule_work(&drd_switch->work);

	return 0;
}

/**
 * exynos_drd_switch_start_peripheral -  bind/unbind the peripheral controller.
 *
 * @otg: Pointer to the usb_otg structure.
 * @on: start / stop the gadget controller driver.
 *
 * Returns 0 on success otherwise negative errno.
 */
static int exynos_drd_switch_start_peripheral(struct usb_otg *otg, int on)
{
	int ret;

	if (!otg->gadget)
		return -EINVAL;

	dev_dbg(otg->phy->dev, "Turn %s gadget %s\n",
			on ? "on" : "off", otg->gadget->name);

	if (on) {
		/* Start device only if host is off */
		if (!exynos_drd_switch_is_host_off(otg)) {
			/*
			 * REVISIT: if host is not suspended shall we check
			 * runtime_error flag and clear it, if it is set?
			 * It will give an additional chance to the host
			 * to be suspended if runtime error happened.
			 */
			dev_vdbg(otg->phy->dev, "%s: host is still active\n",
						__func__);
			return -EAGAIN;
		}

		ret = usb_gadget_vbus_connect(otg->gadget);
	} else {
		ret = usb_gadget_vbus_disconnect(otg->gadget);
		/* Currently always return 0 */
	}

	return ret;
}

/**
 * exynos_drd_switch_set_peripheral -  bind/unbind the peripheral controller driver.
 *
 * @otg: Pointer to the usb_otg structure.
 * @gadget: pointer to the usb_gadget structure.
 *
 * Returns 0 on success otherwise negative errno.
 */
static int exynos_drd_switch_set_peripheral(struct usb_otg *otg,
				struct usb_gadget *gadget)
{
	struct exynos_drd_switch *drd_switch = container_of(otg,
					struct exynos_drd_switch, otg);
	struct exynos_drd *drd = container_of(drd_switch->core,
						struct exynos_drd, core);
	bool activate = false;
	unsigned long flags;

	spin_lock_irqsave(&drd_switch->lock, flags);

	if (gadget) {
		dev_dbg(otg->phy->dev, "Binding gadget %s\n", gadget->name);
		otg->gadget = gadget;

		/*
		 * Prevents unnecessary activation of the work function.
		 * If both peripheral and host are set or if we want to force
		 * peripheral to run then we ensure that work function will
		 * enter to valid state.
		 */
		if (otg->host || (drd->pdata->quirks & FORCE_RUN_PERIPHERAL &&
				  drd_switch->id_state == B_DEV))
			activate = true;
	} else {
		dev_dbg(otg->phy->dev, "Unbinding gadget\n");

		if (otg->phy->state == OTG_STATE_B_PERIPHERAL) {
			exynos_drd_switch_start_peripheral(otg, 0);
			otg->gadget = NULL;
			otg->phy->state = OTG_STATE_UNDEFINED;
			activate = true;
		} else {
			otg->gadget = NULL;
		}
	}

	spin_unlock_irqrestore(&drd_switch->lock, flags);

	if (activate)
		exynos_drd_switch_schedule_work(&drd_switch->work);

	return 0;
}

/**
 * exynos_drd_switch_debounce - GPIO debounce timer handler.
 *
 * @data: Pointer to DRD switch structure represented as unsigned long.
 */
static void exynos_drd_switch_debounce(unsigned long data)
{
	struct exynos_drd_switch *drd_switch =
				(struct exynos_drd_switch *) data;
	struct usb_phy *phy = drd_switch->otg.phy;

	exynos_drd_switch_schedule_work(&drd_switch->work);
	dev_dbg(phy->dev, "new state id: %d, vbus: %d\n",
		drd_switch->id_state, drd_switch->vbus_active ? 1 : 0);
}

/**
 * exynos_drd_switch_handle_vbus - handle VBus state.
 *
 * @drd_switch: Pointer to the DRD switch structure.
 * @vbus_active: VBus state, true if active, false otherwise.
 */
static void exynos_drd_switch_handle_vbus(struct exynos_drd_switch *drd_switch,
					  bool vbus_active)
{
	struct device *dev = drd_switch->otg.phy->dev;
	unsigned long flags;
	int res;

	spin_lock_irqsave(&drd_switch->lock, flags);

	/* REVISIT: handle VBus Change Event only in B-device mode */
	if (drd_switch->id_state != B_DEV)
		goto exit;

	if (vbus_active != drd_switch->vbus_active) {
		drd_switch->vbus_active = vbus_active;
		/*
		 * Debouncing: timer will not expire untill
		 * gpio is stable.
		 */
		res = mod_timer(&drd_switch->vbus_db_timer,
				jiffies + VBUS_DEBOUNCE_DELAY);
		if (res == 1)
			dev_vdbg(dev, "vbus debouncing ...\n");
	}

exit:
	spin_unlock_irqrestore(&drd_switch->lock, flags);
}

/**
 * exynos_drd_switch_handle_id - handle ID pin state.
 *
 * @drd_switch: Pointer to the DRD switch structure.
 * @id_state: ID pin state.
 */
static void exynos_drd_switch_handle_id(struct exynos_drd_switch *drd_switch,
					enum id_pin_state id_state)
{
	struct device *dev = drd_switch->otg.phy->dev;
	unsigned long flags;
	int res;

	spin_lock_irqsave(&drd_switch->lock, flags);

	if (id_state != drd_switch->id_state) {
		drd_switch->id_state = id_state;
		/*
		 * Debouncing: timer will not expire untill
		 * ID state is stable.
		 */
		res = mod_timer(&drd_switch->id_db_timer,
				jiffies + ID_DEBOUNCE_DELAY);
		if (res == 1)
			dev_vdbg(dev, "id debouncing ...\n");
	}

	spin_unlock_irqrestore(&drd_switch->lock, flags);
}

/**
 * exynos_drd_switch_vbus_interrupt - interrupt handler for VBUS GPIO.
 *
 * @irq: irq number.
 * @_drdsw: Pointer to DRD switch structure.
 */
static irqreturn_t exynos_drd_switch_vbus_interrupt(int irq, void *_drdsw)
{
	struct exynos_drd_switch *drd_switch =
				(struct exynos_drd_switch *)_drdsw;
	struct device *dev = drd_switch->otg.phy->dev;
	bool vbus_active;

	vbus_active = exynos_drd_switch_get_bses_vld(drd_switch);

	dev_info(dev, "IRQ: VBUS: %sactive\n", vbus_active ? "" : "in");

	exynos_drd_switch_handle_vbus(drd_switch, vbus_active);

	return IRQ_HANDLED;
}

/**
 * exynos_drd_switch_id_interrupt - interrupt handler for ID GPIO.
 *
 * @irq: irq number.
 * @_drdsw: Pointer to DRD switch structure.
 */
static irqreturn_t exynos_drd_switch_id_interrupt(int irq, void *_drdsw)
{
	struct exynos_drd_switch *drd_switch =
				(struct exynos_drd_switch *)_drdsw;
	struct device *dev = drd_switch->otg.phy->dev;
	enum id_pin_state id_state;

	/*
	 * ID sts has changed, read it and later, in the workqueue
	 * function, switch from A to B or from B to A.
	 */
	id_state = exynos_drd_switch_get_id_state(drd_switch);

	dev_info(dev, "IRQ: ID: %d\n", id_state);

	exynos_drd_switch_handle_id(drd_switch, id_state);

	return IRQ_HANDLED;
}

/**
 * exynos_drd_switch_vbus_event - receive VBus change event.
 *
 * @pdev: DRD that receives the event.
 * @vbus_active: New VBus state, true if active, false otherwise.
 *
 * Other drivers may use this function to tell the role switch driver
 * about the VBus change.
 */
int exynos_drd_switch_vbus_event(struct platform_device *pdev, bool vbus_active)
{
	struct exynos_drd *drd;
	struct usb_otg *otg;
	struct exynos_drd_switch *drd_switch;

	dev_dbg(&pdev->dev, "EVENT: VBUS: %sactive\n", vbus_active ? "" : "in");

	drd = platform_get_drvdata(pdev);
	if (!drd)
		return -ENOENT;

	otg = drd->core.otg;
	if (!otg)
		return -ENOENT;

	drd_switch = container_of(otg, struct exynos_drd_switch, otg);

	exynos_drd_switch_handle_vbus(drd_switch, vbus_active);

	return 0;
}

/**
 * exynos_drd_switch_id_event - receive ID pin state change event.
 *
 * @pdev: DRD that receives the event.
 * @state: New ID pin state.
 *
 * Other drivers may use this function to tell the role switch driver
 * about the ID pin state change.
 */
int exynos_drd_switch_id_event(struct platform_device *pdev, int state)
{
	struct exynos_drd *drd;
	struct usb_otg *otg;
	struct exynos_drd_switch *drd_switch;
	enum id_pin_state id_state;

	dev_dbg(&pdev->dev, "EVENT: ID: %d\n", state);

	drd = platform_get_drvdata(pdev);
	if (!drd)
		return -ENOENT;

	otg = drd->core.otg;
	if (!otg)
		return -ENOENT;

	drd_switch = container_of(otg, struct exynos_drd_switch, otg);

	id_state = exynos_drd_switch_sanitize_id(state);

	exynos_drd_switch_handle_id(drd_switch, id_state);

	return 0;
}

/**
 * exynos_drd_switch_work - work function.
 *
 * @w: Pointer to the exynos otg work structure.
 *
 * NOTE: After any change in phy->state,
 * we must reschdule the state machine.
 */
static void exynos_drd_switch_work(struct work_struct *w)
{
	struct exynos_drd_switch *drd_switch = container_of(w,
					struct exynos_drd_switch, work.work);
	struct usb_phy *phy = drd_switch->otg.phy;
	struct delayed_work *work = &drd_switch->work;
	enum usb_otg_state state, new_state;
	enum id_pin_state id_state;
	bool vbus_active;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&drd_switch->lock, flags);
	state = phy->state;
	id_state = drd_switch->id_state;
	vbus_active = drd_switch->vbus_active;
	spin_unlock_irqrestore(&drd_switch->lock, flags);

	new_state = state;

	/* Check OTG state */
	switch (state) {
	case OTG_STATE_UNDEFINED:
		/* Switch to A or B-Device according to ID state */
		if (id_state == B_DEV)
			new_state = OTG_STATE_B_IDLE;
		else if (id_state == A_DEV)
			new_state = OTG_STATE_A_IDLE;

		exynos_drd_switch_schedule_work(work);
		break;
	case OTG_STATE_B_IDLE:
		if (id_state == A_DEV) {
			new_state = OTG_STATE_A_IDLE;
			exynos_drd_switch_schedule_work(work);
		} else if (vbus_active) {
			/* Start peripheral only if B-Session is valid */
			ret = exynos_drd_switch_start_peripheral(
							&drd_switch->otg, 1);
			if (!ret) {
				new_state = OTG_STATE_B_PERIPHERAL;
				exynos_drd_switch_schedule_work(work);
			} else if (ret == -EAGAIN) {
				exynos_drd_switch_schedule_dwork(work,
								 EAGAIN_DELAY);
			} else {
				/* Fatal error */
				dev_err(phy->dev,
					"unable to start B-device\n");
			}
		} else {
			dev_dbg(phy->dev, "VBus is not active\n");
		}
		break;
	case OTG_STATE_B_PERIPHERAL:
		dev_dbg(phy->dev, "OTG_STATE_B_PERIPHERAL\n");
		if ((id_state == A_DEV) || !vbus_active) {
			exynos_drd_switch_start_peripheral(&drd_switch->otg, 0);
			new_state = OTG_STATE_B_IDLE;
			exynos_drd_switch_schedule_work(work);
		}
		break;
	case OTG_STATE_A_IDLE:
		if (id_state == B_DEV) {
			new_state = OTG_STATE_B_IDLE;
			exynos_drd_switch_schedule_work(work);
		} else {
			/* Switch to A-Device */
			ret = exynos_drd_switch_start_host(&drd_switch->otg, 1);
			if (!ret || ret == -EINPROGRESS) {
				new_state = OTG_STATE_A_HOST;
				exynos_drd_switch_schedule_work(work);
			} else if (ret == -EAGAIN || ret == -EBUSY) {
				dev_vdbg(phy->dev, "host turn on retry\n");
				exynos_drd_switch_schedule_dwork(work,
								 EAGAIN_DELAY);
			} else {
				/* Fatal error */
				dev_err(phy->dev,
						"unable to start A-device\n");
			}
		}
		break;
	case OTG_STATE_A_HOST:
		dev_dbg(phy->dev, "OTG_STATE_A_HOST\n");
		if (id_state == B_DEV) {
			ret = exynos_drd_switch_start_host(&drd_switch->otg, 0);
			/* Currently we ignore most of the errors */
			if (ret == -EAGAIN) {
				dev_vdbg(phy->dev, "host turn off retry\n");
				exynos_drd_switch_schedule_dwork(work,
								 EAGAIN_DELAY);
			} else {
				if (ret == -EINVAL || ret == -EACCES)
					/* Fatal error */
					dev_err(phy->dev,
						"unable to stop A-device\n");

				new_state = OTG_STATE_A_IDLE;
				exynos_drd_switch_schedule_work(work);
			}
		}
		break;
	default:
		dev_err(phy->dev, "invalid otg-state\n");

	}

	spin_lock_irqsave(&drd_switch->lock, flags);
	/*
	 * PHY state could be changed outside of this function.
	 * If so, we don't update the state to prevent overwriting.
	 */
	if (phy->state == state)
		phy->state = new_state;
	spin_unlock_irqrestore(&drd_switch->lock, flags);
}

/**
 * exynos_drd_switch_reset - reset DRD role switch.
 *
 * @drd: Pointer to DRD controller structure.
 * @run: Start sm if 1.
 */
void exynos_drd_switch_reset(struct exynos_drd *drd, int run)
{
	struct usb_otg *otg = drd->core.otg;
	struct exynos_drd_switch *drd_switch;
	unsigned long flags;

	if (otg) {
		drd_switch = container_of(otg,
					struct exynos_drd_switch, otg);

		spin_lock_irqsave(&drd_switch->lock, flags);

		if (drd->pdata->quirks & FORCE_INIT_PERIPHERAL)
			drd_switch->id_state = B_DEV;
		else
			drd_switch->id_state =
				exynos_drd_switch_get_id_state(drd_switch);

		drd_switch->vbus_active =
			exynos_drd_switch_get_bses_vld(drd_switch);

		otg->phy->state = OTG_STATE_UNDEFINED;

		spin_unlock_irqrestore(&drd_switch->lock, flags);

		if (run)
			exynos_drd_switch_schedule_work(&drd_switch->work);

		dev_dbg(drd->dev, "%s: id = %d, vbus = %d\n", __func__,
				drd_switch->id_state, drd_switch->vbus_active);
	}
}

/* /sys/devices/platform/exynos-dwc3.%d/ interface */

static ssize_t
exynos_drd_switch_show_state(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct exynos_drd *drd = dev_get_drvdata(dev);
	struct usb_otg *otg = drd->core.otg;
	struct usb_phy *phy = otg->phy;

	return snprintf(buf, PAGE_SIZE, "%s\n", otg_state_string(phy->state));
}

static DEVICE_ATTR(state, S_IRUSR | S_IRGRP,
	exynos_drd_switch_show_state, NULL);

/*
 * id and vbus attributes allow to change DRD mode and VBus state.
 * Can be used for debug purpose.
 */

static ssize_t
exynos_drd_switch_show_vbus(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct exynos_drd *drd = dev_get_drvdata(dev);
	struct usb_otg *otg = drd->core.otg;
	struct exynos_drd_switch *drd_switch = container_of(otg,
						struct exynos_drd_switch, otg);

	return snprintf(buf, PAGE_SIZE, "%d\n", drd_switch->vbus_active);
}

static ssize_t
exynos_drd_switch_store_vbus(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct exynos_drd *drd = dev_get_drvdata(dev);
	struct usb_otg *otg = drd->core.otg;
	struct exynos_drd_switch *drd_switch = container_of(otg,
						struct exynos_drd_switch, otg);
	int vbus_active;

	if (sscanf(buf, "%d", &vbus_active) != 1)
		return -EINVAL;
	exynos_drd_switch_handle_vbus(drd_switch, !!vbus_active);

	return n;
}

static DEVICE_ATTR(vbus, S_IWUSR | S_IRUSR | S_IRGRP,
	exynos_drd_switch_show_vbus, exynos_drd_switch_store_vbus);

static ssize_t
exynos_drd_switch_show_id(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct exynos_drd *drd = dev_get_drvdata(dev);
	struct usb_otg *otg = drd->core.otg;
	struct exynos_drd_switch *drd_switch = container_of(otg,
						struct exynos_drd_switch, otg);

	return snprintf(buf, PAGE_SIZE, "%d\n", drd_switch->id_state);
}

static ssize_t
exynos_drd_switch_store_id(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct exynos_drd *drd = dev_get_drvdata(dev);
	struct usb_otg *otg = drd->core.otg;
	struct exynos_drd_switch *drd_switch = container_of(otg,
						struct exynos_drd_switch, otg);
	int state;
	enum id_pin_state id_state;

	if (sscanf(buf, "%d", &state) != 1)
		return -EINVAL;
	id_state = exynos_drd_switch_sanitize_id(state);
	exynos_drd_switch_handle_id(drd_switch, id_state);

	return n;
}

static DEVICE_ATTR(id, S_IWUSR | S_IRUSR | S_IRGRP,
	exynos_drd_switch_show_id, exynos_drd_switch_store_id);

static struct attribute *exynos_drd_switch_attributes[] = {
	&dev_attr_id.attr,
	&dev_attr_vbus.attr,
	&dev_attr_state.attr,
	NULL
};

static const struct attribute_group exynos_drd_switch_attr_group = {
	.attrs = exynos_drd_switch_attributes,
};

/**
 * exynos_drd_switch_init - Initializes DRD role switch.
 *
 * @drd: Pointer to DRD controller structure.
 *
 * Returns 0 on success otherwise negative errno.
 */
int exynos_drd_switch_init(struct exynos_drd *drd)
{
	struct dwc3_exynos_data *pdata = drd->pdata;
	struct exynos_drd_switch *drd_switch;
	int ret = 0;
	unsigned long irq_flags = 0;

	dev_dbg(drd->dev, "%s\n", __func__);

	drd_switch = devm_kzalloc(drd->dev, sizeof(struct exynos_drd_switch),
				  GFP_KERNEL);
	if (!drd_switch) {
		dev_err(drd->dev, "not enough memory for DRD switch\n");
		return -ENOMEM;
	}

	drd_switch->core = &drd->core;

	/* ID pin gpio IRQ */
	drd_switch->id_irq = pdata->id_irq;
	if (drd_switch->id_irq < 0)
		dev_dbg(drd->dev, "cannot find ID irq\n");

	init_timer(&drd_switch->id_db_timer);
	drd_switch->id_db_timer.data = (unsigned long) drd_switch;
	drd_switch->id_db_timer.function = exynos_drd_switch_debounce;

	/* VBus pin gpio IRQ */
	drd_switch->vbus_irq = pdata->vbus_irq;
	if (drd_switch->vbus_irq < 0)
		dev_dbg(drd->dev, "cannot find VBUS irq\n");

	init_timer(&drd_switch->vbus_db_timer);
	drd_switch->vbus_db_timer.data = (unsigned long) drd_switch;
	drd_switch->vbus_db_timer.function = exynos_drd_switch_debounce;

	irq_flags = pdata->irq_flags;

	drd_switch->otg.set_peripheral = exynos_drd_switch_set_peripheral;
	drd_switch->otg.set_host = exynos_drd_switch_set_host;

	/* Save for using by host and peripheral */
	drd->core.otg = &drd_switch->otg;

	drd_switch->otg.phy = devm_kzalloc(drd->dev, sizeof(struct usb_phy),
					   GFP_KERNEL);
	if (!drd_switch->otg.phy) {
		dev_err(drd->dev, "cannot allocate OTG phy\n");
		return -ENOMEM;
	}

	drd_switch->otg.phy->otg = &drd_switch->otg;
	drd_switch->otg.phy->dev = drd->dev;
#if 0
	/*
	 * TODO: we need to have support for multiple transceivers here.
	 * Kernel > 3.5 should already have it. Now it works only for one
	 * drd channel.
	 */
	ret = usb_set_transceiver(drd_switch->otg.phy);
	if (ret) {
		dev_err(drd->dev,
			"failed to set transceiver, already exists\n",
			__func__);
		goto err2;
	}
#endif
	spin_lock_init(&drd_switch->lock);

	wake_lock_init(&drd_switch->wakelock,
		WAKE_LOCK_SUSPEND, "drd_switch");

	exynos_drd_switch_reset(drd, 0);

	drd_switch->wq = create_freezable_workqueue("drd_switch");
	if (!drd_switch->wq) {
		dev_err(drd->dev, "cannot create workqueue\n");
		ret = -ENOMEM;
		goto err_wq;
	}

	INIT_DELAYED_WORK(&drd_switch->work, exynos_drd_switch_work);

	if (drd_switch->id_irq >= 0) {
		ret = devm_request_irq(drd->dev, drd_switch->id_irq,
				  exynos_drd_switch_id_interrupt, irq_flags,
				  "drd_switch_id", drd_switch);
		if (ret) {
			dev_err(drd->dev, "cannot claim ID irq\n");
			goto err_irq;
		}
	}

	if (drd_switch->vbus_irq >= 0) {
		ret = devm_request_irq(drd->dev, drd_switch->vbus_irq,
				  exynos_drd_switch_vbus_interrupt, irq_flags,
				  "drd_switch_vbus", drd_switch);
		if (ret) {
			dev_err(drd->dev, "cannot claim VBUS irq\n");
			goto err_irq;
		}
	}

	ret = sysfs_create_group(&drd->dev->kobj, &exynos_drd_switch_attr_group);
	if (ret) {
		dev_err(drd->dev, "cannot create switch attributes\n");
		goto err_irq;
	}

	dev_dbg(drd->dev, "DRD switch initialization finished normally\n");

	return 0;

err_irq:
	cancel_delayed_work_sync(&drd_switch->work);
	destroy_workqueue(drd_switch->wq);
err_wq:
	wake_lock_destroy(&drd_switch->wakelock);

	return ret;
}

/**
 * exynos_drd_switch_exit
 *
 * @drd: Pointer to DRD controller structure
 *
 * Returns 0 on success otherwise negative errno.
 */
void exynos_drd_switch_exit(struct exynos_drd *drd)
{
	struct usb_otg *otg = drd->core.otg;
	struct exynos_drd_switch *drd_switch;

	if (otg) {
		drd_switch = container_of(otg,
					struct exynos_drd_switch, otg);

		wake_lock_destroy(&drd_switch->wakelock);
		sysfs_remove_group(&drd->dev->kobj,
			&exynos_drd_switch_attr_group);
		cancel_delayed_work_sync(&drd_switch->work);
		destroy_workqueue(drd_switch->wq);
	}
}
