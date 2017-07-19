/*
 * Copyright (C) 2011 Marvell International Ltd. All rights reserved.
 * Author: Chao Xie <chao.xie@marvell.com>
 *	   Neil Zhang <zhangwm@marvell.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/clk.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>

#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/otg.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/platform_data/mv_usb.h>

#include "phy-mv-usb.h"

#define	DRIVER_DESC	"Marvell USB OTG transceiver driver"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

static const char driver_name[] = "mv-otg";

static char *state_string[] = {
	"undefined",
	"b_idle",
	"b_srp_init",
	"b_peripheral",
	"b_wait_acon",
	"b_host",
	"a_idle",
	"a_wait_vrise",
	"a_wait_bcon",
	"a_host",
	"a_suspend",
	"a_peripheral",
	"a_wait_vfall",
	"a_vbus_err"
};

static int mv_otg_set_vbus(struct usb_otg *otg, bool on)
{
	struct mv_otg *mvotg = container_of(otg->usb_phy, struct mv_otg, phy);
	if (mvotg->pdata->set_vbus == NULL)
		return -ENODEV;

	return mvotg->pdata->set_vbus(on);
}

static int mv_otg_set_host(struct usb_otg *otg,
			   struct usb_bus *host)
{
	otg->host = host;

	return 0;
}

static int mv_otg_set_peripheral(struct usb_otg *otg,
				 struct usb_gadget *gadget)
{
	otg->gadget = gadget;

	return 0;
}

static void mv_otg_run_state_machine(struct mv_otg *mvotg,
				     unsigned long delay)
{
	dev_dbg(&mvotg->pdev->dev, "transceiver is updated\n");
	if (!mvotg->qwork)
		return;

	queue_delayed_work(mvotg->qwork, &mvotg->work, delay);
}

static void mv_otg_timer_await_bcon(unsigned long data)
{
	struct mv_otg *mvotg = (struct mv_otg *) data;

	mvotg->otg_ctrl.a_wait_bcon_timeout = 1;

	dev_info(&mvotg->pdev->dev, "B Device No Response!\n");

	if (spin_trylock(&mvotg->wq_lock)) {
		mv_otg_run_state_machine(mvotg, 0);
		spin_unlock(&mvotg->wq_lock);
	}
}

static int mv_otg_cancel_timer(struct mv_otg *mvotg, unsigned int id)
{
	struct timer_list *timer;

	if (id >= OTG_TIMER_NUM)
		return -EINVAL;

	timer = &mvotg->otg_ctrl.timer[id];

	if (timer_pending(timer))
		del_timer(timer);

	return 0;
}

static int mv_otg_set_timer(struct mv_otg *mvotg, unsigned int id,
			    unsigned long interval,
			    void (*callback) (unsigned long))
{
	struct timer_list *timer;

	if (id >= OTG_TIMER_NUM)
		return -EINVAL;

	timer = &mvotg->otg_ctrl.timer[id];
	if (timer_pending(timer)) {
		dev_err(&mvotg->pdev->dev, "Timer%d is already running\n", id);
		return -EBUSY;
	}

	init_timer(timer);
	timer->data = (unsigned long) mvotg;
	timer->function = callback;
	timer->expires = jiffies + interval;
	add_timer(timer);

	return 0;
}

static int mv_otg_reset(struct mv_otg *mvotg)
{
	unsigned int loops;
	u32 tmp;

	/* Stop the controller */
	tmp = readl(&mvotg->op_regs->usbcmd);
	tmp &= ~USBCMD_RUN_STOP;
	writel(tmp, &mvotg->op_regs->usbcmd);

	/* Reset the controller to get default values */
	writel(USBCMD_CTRL_RESET, &mvotg->op_regs->usbcmd);

	loops = 500;
	while (readl(&mvotg->op_regs->usbcmd) & USBCMD_CTRL_RESET) {
		if (loops == 0) {
			dev_err(&mvotg->pdev->dev,
				"Wait for RESET completed TIMEOUT\n");
			return -ETIMEDOUT;
		}
		loops--;
		udelay(20);
	}

	writel(0x0, &mvotg->op_regs->usbintr);
	tmp = readl(&mvotg->op_regs->usbsts);
	writel(tmp, &mvotg->op_regs->usbsts);

	return 0;
}

static void mv_otg_init_irq(struct mv_otg *mvotg)
{
	u32 otgsc;

	mvotg->irq_en = OTGSC_INTR_A_SESSION_VALID
	    | OTGSC_INTR_A_VBUS_VALID;
	mvotg->irq_status = OTGSC_INTSTS_A_SESSION_VALID
	    | OTGSC_INTSTS_A_VBUS_VALID;

	if (mvotg->pdata->vbus == NULL) {
		mvotg->irq_en |= OTGSC_INTR_B_SESSION_VALID
		    | OTGSC_INTR_B_SESSION_END;
		mvotg->irq_status |= OTGSC_INTSTS_B_SESSION_VALID
		    | OTGSC_INTSTS_B_SESSION_END;
	}

	if (mvotg->pdata->id == NULL) {
		mvotg->irq_en |= OTGSC_INTR_USB_ID;
		mvotg->irq_status |= OTGSC_INTSTS_USB_ID;
	}

	otgsc = readl(&mvotg->op_regs->otgsc);
	otgsc |= mvotg->irq_en;
	writel(otgsc, &mvotg->op_regs->otgsc);
}

static void mv_otg_start_host(struct mv_otg *mvotg, int on)
{
#ifdef CONFIG_USB
	struct usb_otg *otg = mvotg->phy.otg;
	struct usb_hcd *hcd;

	if (!otg->host)
		return;

	dev_info(&mvotg->pdev->dev, "%s host\n", on ? "start" : "stop");

	hcd = bus_to_hcd(otg->host);

	if (on) {
		usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
		device_wakeup_enable(hcd->self.controller);
	} else {
		usb_remove_hcd(hcd);
	}
#endif /* CONFIG_USB */
}

static void mv_otg_start_periphrals(struct mv_otg *mvotg, int on)
{
	struct usb_otg *otg = mvotg->phy.otg;

	if (!otg->gadget)
		return;

	dev_info(mvotg->phy.dev, "gadget %s\n", on ? "on" : "off");

	if (on)
		usb_gadget_vbus_connect(otg->gadget);
	else
		usb_gadget_vbus_disconnect(otg->gadget);
}

static void otg_clock_enable(struct mv_otg *mvotg)
{
	clk_prepare_enable(mvotg->clk);
}

static void otg_clock_disable(struct mv_otg *mvotg)
{
	clk_disable_unprepare(mvotg->clk);
}

static int mv_otg_enable_internal(struct mv_otg *mvotg)
{
	int retval = 0;

	if (mvotg->active)
		return 0;

	dev_dbg(&mvotg->pdev->dev, "otg enabled\n");

	otg_clock_enable(mvotg);
	if (mvotg->pdata->phy_init) {
		retval = mvotg->pdata->phy_init(mvotg->phy_regs);
		if (retval) {
			dev_err(&mvotg->pdev->dev,
				"init phy error %d\n", retval);
			otg_clock_disable(mvotg);
			return retval;
		}
	}
	mvotg->active = 1;

	return 0;

}

static int mv_otg_enable(struct mv_otg *mvotg)
{
	if (mvotg->clock_gating)
		return mv_otg_enable_internal(mvotg);

	return 0;
}

static void mv_otg_disable_internal(struct mv_otg *mvotg)
{
	if (mvotg->active) {
		dev_dbg(&mvotg->pdev->dev, "otg disabled\n");
		if (mvotg->pdata->phy_deinit)
			mvotg->pdata->phy_deinit(mvotg->phy_regs);
		otg_clock_disable(mvotg);
		mvotg->active = 0;
	}
}

static void mv_otg_disable(struct mv_otg *mvotg)
{
	if (mvotg->clock_gating)
		mv_otg_disable_internal(mvotg);
}

static void mv_otg_update_inputs(struct mv_otg *mvotg)
{
	struct mv_otg_ctrl *otg_ctrl = &mvotg->otg_ctrl;
	u32 otgsc;

	otgsc = readl(&mvotg->op_regs->otgsc);

	if (mvotg->pdata->vbus) {
		if (mvotg->pdata->vbus->poll() == VBUS_HIGH) {
			otg_ctrl->b_sess_vld = 1;
			otg_ctrl->b_sess_end = 0;
		} else {
			otg_ctrl->b_sess_vld = 0;
			otg_ctrl->b_sess_end = 1;
		}
	} else {
		otg_ctrl->b_sess_vld = !!(otgsc & OTGSC_STS_B_SESSION_VALID);
		otg_ctrl->b_sess_end = !!(otgsc & OTGSC_STS_B_SESSION_END);
	}

	if (mvotg->pdata->id)
		otg_ctrl->id = !!mvotg->pdata->id->poll();
	else
		otg_ctrl->id = !!(otgsc & OTGSC_STS_USB_ID);

	if (mvotg->pdata->otg_force_a_bus_req && !otg_ctrl->id)
		otg_ctrl->a_bus_req = 1;

	otg_ctrl->a_sess_vld = !!(otgsc & OTGSC_STS_A_SESSION_VALID);
	otg_ctrl->a_vbus_vld = !!(otgsc & OTGSC_STS_A_VBUS_VALID);

	dev_dbg(&mvotg->pdev->dev, "%s: ", __func__);
	dev_dbg(&mvotg->pdev->dev, "id %d\n", otg_ctrl->id);
	dev_dbg(&mvotg->pdev->dev, "b_sess_vld %d\n", otg_ctrl->b_sess_vld);
	dev_dbg(&mvotg->pdev->dev, "b_sess_end %d\n", otg_ctrl->b_sess_end);
	dev_dbg(&mvotg->pdev->dev, "a_vbus_vld %d\n", otg_ctrl->a_vbus_vld);
	dev_dbg(&mvotg->pdev->dev, "a_sess_vld %d\n", otg_ctrl->a_sess_vld);
}

static void mv_otg_update_state(struct mv_otg *mvotg)
{
	struct mv_otg_ctrl *otg_ctrl = &mvotg->otg_ctrl;
	int old_state = mvotg->phy.otg->state;

	switch (old_state) {
	case OTG_STATE_UNDEFINED:
		mvotg->phy.otg->state = OTG_STATE_B_IDLE;
		/* FALL THROUGH */
	case OTG_STATE_B_IDLE:
		if (otg_ctrl->id == 0)
			mvotg->phy.otg->state = OTG_STATE_A_IDLE;
		else if (otg_ctrl->b_sess_vld)
			mvotg->phy.otg->state = OTG_STATE_B_PERIPHERAL;
		break;
	case OTG_STATE_B_PERIPHERAL:
		if (!otg_ctrl->b_sess_vld || otg_ctrl->id == 0)
			mvotg->phy.otg->state = OTG_STATE_B_IDLE;
		break;
	case OTG_STATE_A_IDLE:
		if (otg_ctrl->id)
			mvotg->phy.otg->state = OTG_STATE_B_IDLE;
		else if (!(otg_ctrl->a_bus_drop) &&
			 (otg_ctrl->a_bus_req || otg_ctrl->a_srp_det))
			mvotg->phy.otg->state = OTG_STATE_A_WAIT_VRISE;
		break;
	case OTG_STATE_A_WAIT_VRISE:
		if (otg_ctrl->a_vbus_vld)
			mvotg->phy.otg->state = OTG_STATE_A_WAIT_BCON;
		break;
	case OTG_STATE_A_WAIT_BCON:
		if (otg_ctrl->id || otg_ctrl->a_bus_drop
		    || otg_ctrl->a_wait_bcon_timeout) {
			mv_otg_cancel_timer(mvotg, A_WAIT_BCON_TIMER);
			mvotg->otg_ctrl.a_wait_bcon_timeout = 0;
			mvotg->phy.otg->state = OTG_STATE_A_WAIT_VFALL;
			otg_ctrl->a_bus_req = 0;
		} else if (!otg_ctrl->a_vbus_vld) {
			mv_otg_cancel_timer(mvotg, A_WAIT_BCON_TIMER);
			mvotg->otg_ctrl.a_wait_bcon_timeout = 0;
			mvotg->phy.otg->state = OTG_STATE_A_VBUS_ERR;
		} else if (otg_ctrl->b_conn) {
			mv_otg_cancel_timer(mvotg, A_WAIT_BCON_TIMER);
			mvotg->otg_ctrl.a_wait_bcon_timeout = 0;
			mvotg->phy.otg->state = OTG_STATE_A_HOST;
		}
		break;
	case OTG_STATE_A_HOST:
		if (otg_ctrl->id || !otg_ctrl->b_conn
		    || otg_ctrl->a_bus_drop)
			mvotg->phy.otg->state = OTG_STATE_A_WAIT_BCON;
		else if (!otg_ctrl->a_vbus_vld)
			mvotg->phy.otg->state = OTG_STATE_A_VBUS_ERR;
		break;
	case OTG_STATE_A_WAIT_VFALL:
		if (otg_ctrl->id
		    || (!otg_ctrl->b_conn && otg_ctrl->a_sess_vld)
		    || otg_ctrl->a_bus_req)
			mvotg->phy.otg->state = OTG_STATE_A_IDLE;
		break;
	case OTG_STATE_A_VBUS_ERR:
		if (otg_ctrl->id || otg_ctrl->a_clr_err
		    || otg_ctrl->a_bus_drop) {
			otg_ctrl->a_clr_err = 0;
			mvotg->phy.otg->state = OTG_STATE_A_WAIT_VFALL;
		}
		break;
	default:
		break;
	}
}

static void mv_otg_work(struct work_struct *work)
{
	struct mv_otg *mvotg;
	struct usb_phy *phy;
	struct usb_otg *otg;
	int old_state;

	mvotg = container_of(to_delayed_work(work), struct mv_otg, work);

run:
	/* work queue is single thread, or we need spin_lock to protect */
	phy = &mvotg->phy;
	otg = mvotg->phy.otg;
	old_state = otg->state;

	if (!mvotg->active)
		return;

	mv_otg_update_inputs(mvotg);
	mv_otg_update_state(mvotg);

	if (old_state != mvotg->phy.otg->state) {
		dev_info(&mvotg->pdev->dev, "change from state %s to %s\n",
			 state_string[old_state],
			 state_string[mvotg->phy.otg->state]);

		switch (mvotg->phy.otg->state) {
		case OTG_STATE_B_IDLE:
			otg->default_a = 0;
			if (old_state == OTG_STATE_B_PERIPHERAL)
				mv_otg_start_periphrals(mvotg, 0);
			mv_otg_reset(mvotg);
			mv_otg_disable(mvotg);
			usb_phy_set_event(&mvotg->phy, USB_EVENT_NONE);
			break;
		case OTG_STATE_B_PERIPHERAL:
			mv_otg_enable(mvotg);
			mv_otg_start_periphrals(mvotg, 1);
			usb_phy_set_event(&mvotg->phy, USB_EVENT_ENUMERATED);
			break;
		case OTG_STATE_A_IDLE:
			otg->default_a = 1;
			mv_otg_enable(mvotg);
			if (old_state == OTG_STATE_A_WAIT_VFALL)
				mv_otg_start_host(mvotg, 0);
			mv_otg_reset(mvotg);
			break;
		case OTG_STATE_A_WAIT_VRISE:
			mv_otg_set_vbus(otg, 1);
			break;
		case OTG_STATE_A_WAIT_BCON:
			if (old_state != OTG_STATE_A_HOST)
				mv_otg_start_host(mvotg, 1);
			mv_otg_set_timer(mvotg, A_WAIT_BCON_TIMER,
					 T_A_WAIT_BCON,
					 mv_otg_timer_await_bcon);
			/*
			 * Now, we directly enter A_HOST. So set b_conn = 1
			 * here. In fact, it need host driver to notify us.
			 */
			mvotg->otg_ctrl.b_conn = 1;
			break;
		case OTG_STATE_A_HOST:
			break;
		case OTG_STATE_A_WAIT_VFALL:
			/*
			 * Now, we has exited A_HOST. So set b_conn = 0
			 * here. In fact, it need host driver to notify us.
			 */
			mvotg->otg_ctrl.b_conn = 0;
			mv_otg_set_vbus(otg, 0);
			break;
		case OTG_STATE_A_VBUS_ERR:
			break;
		default:
			break;
		}
		goto run;
	}
}

static irqreturn_t mv_otg_irq(int irq, void *dev)
{
	struct mv_otg *mvotg = dev;
	u32 otgsc;

	otgsc = readl(&mvotg->op_regs->otgsc);
	writel(otgsc, &mvotg->op_regs->otgsc);

	/*
	 * if we have vbus, then the vbus detection for B-device
	 * will be done by mv_otg_inputs_irq().
	 */
	if (mvotg->pdata->vbus)
		if ((otgsc & OTGSC_STS_USB_ID) &&
		    !(otgsc & OTGSC_INTSTS_USB_ID))
			return IRQ_NONE;

	if ((otgsc & mvotg->irq_status) == 0)
		return IRQ_NONE;

	mv_otg_run_state_machine(mvotg, 0);

	return IRQ_HANDLED;
}

static irqreturn_t mv_otg_inputs_irq(int irq, void *dev)
{
	struct mv_otg *mvotg = dev;

	/* The clock may disabled at this time */
	if (!mvotg->active) {
		mv_otg_enable(mvotg);
		mv_otg_init_irq(mvotg);
	}

	mv_otg_run_state_machine(mvotg, 0);

	return IRQ_HANDLED;
}

static ssize_t
get_a_bus_req(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mv_otg *mvotg = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 mvotg->otg_ctrl.a_bus_req);
}

static ssize_t
set_a_bus_req(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct mv_otg *mvotg = dev_get_drvdata(dev);

	if (count > 2)
		return -1;

	/* We will use this interface to change to A device */
	if (mvotg->phy.otg->state != OTG_STATE_B_IDLE
	    && mvotg->phy.otg->state != OTG_STATE_A_IDLE)
		return -1;

	/* The clock may disabled and we need to set irq for ID detected */
	mv_otg_enable(mvotg);
	mv_otg_init_irq(mvotg);

	if (buf[0] == '1') {
		mvotg->otg_ctrl.a_bus_req = 1;
		mvotg->otg_ctrl.a_bus_drop = 0;
		dev_dbg(&mvotg->pdev->dev,
			"User request: a_bus_req = 1\n");

		if (spin_trylock(&mvotg->wq_lock)) {
			mv_otg_run_state_machine(mvotg, 0);
			spin_unlock(&mvotg->wq_lock);
		}
	}

	return count;
}

static DEVICE_ATTR(a_bus_req, S_IRUGO | S_IWUSR, get_a_bus_req,
		   set_a_bus_req);

static ssize_t
set_a_clr_err(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct mv_otg *mvotg = dev_get_drvdata(dev);
	if (!mvotg->phy.otg->default_a)
		return -1;

	if (count > 2)
		return -1;

	if (buf[0] == '1') {
		mvotg->otg_ctrl.a_clr_err = 1;
		dev_dbg(&mvotg->pdev->dev,
			"User request: a_clr_err = 1\n");
	}

	if (spin_trylock(&mvotg->wq_lock)) {
		mv_otg_run_state_machine(mvotg, 0);
		spin_unlock(&mvotg->wq_lock);
	}

	return count;
}

static DEVICE_ATTR(a_clr_err, S_IWUSR, NULL, set_a_clr_err);

static ssize_t
get_a_bus_drop(struct device *dev, struct device_attribute *attr,
	       char *buf)
{
	struct mv_otg *mvotg = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 mvotg->otg_ctrl.a_bus_drop);
}

static ssize_t
set_a_bus_drop(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct mv_otg *mvotg = dev_get_drvdata(dev);
	if (!mvotg->phy.otg->default_a)
		return -1;

	if (count > 2)
		return -1;

	if (buf[0] == '0') {
		mvotg->otg_ctrl.a_bus_drop = 0;
		dev_dbg(&mvotg->pdev->dev,
			"User request: a_bus_drop = 0\n");
	} else if (buf[0] == '1') {
		mvotg->otg_ctrl.a_bus_drop = 1;
		mvotg->otg_ctrl.a_bus_req = 0;
		dev_dbg(&mvotg->pdev->dev,
			"User request: a_bus_drop = 1\n");
		dev_dbg(&mvotg->pdev->dev,
			"User request: and a_bus_req = 0\n");
	}

	if (spin_trylock(&mvotg->wq_lock)) {
		mv_otg_run_state_machine(mvotg, 0);
		spin_unlock(&mvotg->wq_lock);
	}

	return count;
}

static DEVICE_ATTR(a_bus_drop, S_IRUGO | S_IWUSR,
		   get_a_bus_drop, set_a_bus_drop);

static struct attribute *inputs_attrs[] = {
	&dev_attr_a_bus_req.attr,
	&dev_attr_a_clr_err.attr,
	&dev_attr_a_bus_drop.attr,
	NULL,
};

static struct attribute_group inputs_attr_group = {
	.name = "inputs",
	.attrs = inputs_attrs,
};

static int mv_otg_remove(struct platform_device *pdev)
{
	struct mv_otg *mvotg = platform_get_drvdata(pdev);

	sysfs_remove_group(&mvotg->pdev->dev.kobj, &inputs_attr_group);

	if (mvotg->qwork) {
		flush_workqueue(mvotg->qwork);
		destroy_workqueue(mvotg->qwork);
	}

	mv_otg_disable(mvotg);

	usb_remove_phy(&mvotg->phy);

	return 0;
}

static int mv_otg_probe(struct platform_device *pdev)
{
	struct mv_usb_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct mv_otg *mvotg;
	struct usb_otg *otg;
	struct resource *r;
	int retval = 0, i;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "failed to get platform data\n");
		return -ENODEV;
	}

	mvotg = devm_kzalloc(&pdev->dev, sizeof(*mvotg), GFP_KERNEL);
	if (!mvotg)
		return -ENOMEM;

	otg = devm_kzalloc(&pdev->dev, sizeof(*otg), GFP_KERNEL);
	if (!otg)
		return -ENOMEM;

	platform_set_drvdata(pdev, mvotg);

	mvotg->pdev = pdev;
	mvotg->pdata = pdata;

	mvotg->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mvotg->clk))
		return PTR_ERR(mvotg->clk);

	mvotg->qwork = create_singlethread_workqueue("mv_otg_queue");
	if (!mvotg->qwork) {
		dev_dbg(&pdev->dev, "cannot create workqueue for OTG\n");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&mvotg->work, mv_otg_work);

	/* OTG common part */
	mvotg->pdev = pdev;
	mvotg->phy.dev = &pdev->dev;
	mvotg->phy.otg = otg;
	mvotg->phy.label = driver_name;

	otg->state = OTG_STATE_UNDEFINED;
	otg->usb_phy = &mvotg->phy;
	otg->set_host = mv_otg_set_host;
	otg->set_peripheral = mv_otg_set_peripheral;
	otg->set_vbus = mv_otg_set_vbus;

	for (i = 0; i < OTG_TIMER_NUM; i++)
		init_timer(&mvotg->otg_ctrl.timer[i]);

	r = platform_get_resource_byname(mvotg->pdev,
					 IORESOURCE_MEM, "phyregs");
	if (r == NULL) {
		dev_err(&pdev->dev, "no phy I/O memory resource defined\n");
		retval = -ENODEV;
		goto err_destroy_workqueue;
	}

	mvotg->phy_regs = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (mvotg->phy_regs == NULL) {
		dev_err(&pdev->dev, "failed to map phy I/O memory\n");
		retval = -EFAULT;
		goto err_destroy_workqueue;
	}

	r = platform_get_resource_byname(mvotg->pdev,
					 IORESOURCE_MEM, "capregs");
	if (r == NULL) {
		dev_err(&pdev->dev, "no I/O memory resource defined\n");
		retval = -ENODEV;
		goto err_destroy_workqueue;
	}

	mvotg->cap_regs = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (mvotg->cap_regs == NULL) {
		dev_err(&pdev->dev, "failed to map I/O memory\n");
		retval = -EFAULT;
		goto err_destroy_workqueue;
	}

	/* we will acces controller register, so enable the udc controller */
	retval = mv_otg_enable_internal(mvotg);
	if (retval) {
		dev_err(&pdev->dev, "mv otg enable error %d\n", retval);
		goto err_destroy_workqueue;
	}

	mvotg->op_regs =
		(struct mv_otg_regs __iomem *) ((unsigned long) mvotg->cap_regs
			+ (readl(mvotg->cap_regs) & CAPLENGTH_MASK));

	if (pdata->id) {
		retval = devm_request_threaded_irq(&pdev->dev, pdata->id->irq,
						NULL, mv_otg_inputs_irq,
						IRQF_ONESHOT, "id", mvotg);
		if (retval) {
			dev_info(&pdev->dev,
				 "Failed to request irq for ID\n");
			pdata->id = NULL;
		}
	}

	if (pdata->vbus) {
		mvotg->clock_gating = 1;
		retval = devm_request_threaded_irq(&pdev->dev, pdata->vbus->irq,
						NULL, mv_otg_inputs_irq,
						IRQF_ONESHOT, "vbus", mvotg);
		if (retval) {
			dev_info(&pdev->dev,
				 "Failed to request irq for VBUS, "
				 "disable clock gating\n");
			mvotg->clock_gating = 0;
			pdata->vbus = NULL;
		}
	}

	if (pdata->disable_otg_clock_gating)
		mvotg->clock_gating = 0;

	mv_otg_reset(mvotg);
	mv_otg_init_irq(mvotg);

	r = platform_get_resource(mvotg->pdev, IORESOURCE_IRQ, 0);
	if (r == NULL) {
		dev_err(&pdev->dev, "no IRQ resource defined\n");
		retval = -ENODEV;
		goto err_disable_clk;
	}

	mvotg->irq = r->start;
	if (devm_request_irq(&pdev->dev, mvotg->irq, mv_otg_irq, IRQF_SHARED,
			driver_name, mvotg)) {
		dev_err(&pdev->dev, "Request irq %d for OTG failed\n",
			mvotg->irq);
		mvotg->irq = 0;
		retval = -ENODEV;
		goto err_disable_clk;
	}

	retval = usb_add_phy(&mvotg->phy, USB_PHY_TYPE_USB2);
	if (retval < 0) {
		dev_err(&pdev->dev, "can't register transceiver, %d\n",
			retval);
		goto err_disable_clk;
	}

	retval = sysfs_create_group(&pdev->dev.kobj, &inputs_attr_group);
	if (retval < 0) {
		dev_dbg(&pdev->dev,
			"Can't register sysfs attr group: %d\n", retval);
		goto err_remove_phy;
	}

	spin_lock_init(&mvotg->wq_lock);
	if (spin_trylock(&mvotg->wq_lock)) {
		mv_otg_run_state_machine(mvotg, 2 * HZ);
		spin_unlock(&mvotg->wq_lock);
	}

	dev_info(&pdev->dev,
		 "successful probe OTG device %s clock gating.\n",
		 mvotg->clock_gating ? "with" : "without");

	return 0;

err_remove_phy:
	usb_remove_phy(&mvotg->phy);
err_disable_clk:
	mv_otg_disable_internal(mvotg);
err_destroy_workqueue:
	flush_workqueue(mvotg->qwork);
	destroy_workqueue(mvotg->qwork);

	return retval;
}

#ifdef CONFIG_PM
static int mv_otg_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct mv_otg *mvotg = platform_get_drvdata(pdev);

	if (mvotg->phy.otg->state != OTG_STATE_B_IDLE) {
		dev_info(&pdev->dev,
			 "OTG state is not B_IDLE, it is %d!\n",
			 mvotg->phy.otg->state);
		return -EAGAIN;
	}

	if (!mvotg->clock_gating)
		mv_otg_disable_internal(mvotg);

	return 0;
}

static int mv_otg_resume(struct platform_device *pdev)
{
	struct mv_otg *mvotg = platform_get_drvdata(pdev);
	u32 otgsc;

	if (!mvotg->clock_gating) {
		mv_otg_enable_internal(mvotg);

		otgsc = readl(&mvotg->op_regs->otgsc);
		otgsc |= mvotg->irq_en;
		writel(otgsc, &mvotg->op_regs->otgsc);

		if (spin_trylock(&mvotg->wq_lock)) {
			mv_otg_run_state_machine(mvotg, 0);
			spin_unlock(&mvotg->wq_lock);
		}
	}
	return 0;
}
#endif

static struct platform_driver mv_otg_driver = {
	.probe = mv_otg_probe,
	.remove = mv_otg_remove,
	.driver = {
		   .name = driver_name,
		   },
#ifdef CONFIG_PM
	.suspend = mv_otg_suspend,
	.resume = mv_otg_resume,
#endif
};
module_platform_driver(mv_otg_driver);
