/*
 * otg-wakelock.c
 *
 * Copyright (C) 2011 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/usb/otg.h>

#define TEMPORARY_HOLD_TIME	2000

static bool enabled = true;
static struct usb_phy *otgwl_xceiv;
static struct notifier_block otgwl_nb;

/*
 * otgwl_spinlock is held while the VBUS lock is grabbed or dropped and the
 * held field is updated to match.
 */

static DEFINE_SPINLOCK(otgwl_spinlock);

/*
 * Only one lock, but since these 3 fields are associated with each other...
 */

struct otgwl_lock {
	char name[40];
	struct wakeup_source wakesrc;
	bool held;
};

/*
 * VBUS present lock.  Also used as a timed lock on charger
 * connect/disconnect and USB host disconnect, to allow the system
 * to react to the change in power.
 */

static struct otgwl_lock vbus_lock;

static void otgwl_hold(struct otgwl_lock *lock)
{
	if (!lock->held) {
		__pm_stay_awake(&lock->wakesrc);
		lock->held = true;
	}
}

static void otgwl_temporary_hold(struct otgwl_lock *lock)
{
	__pm_wakeup_event(&lock->wakesrc, TEMPORARY_HOLD_TIME);
	lock->held = false;
}

static void otgwl_drop(struct otgwl_lock *lock)
{
	if (lock->held) {
		__pm_relax(&lock->wakesrc);
		lock->held = false;
	}
}

static void otgwl_handle_event(unsigned long event)
{
	unsigned long irqflags;

	spin_lock_irqsave(&otgwl_spinlock, irqflags);

	if (!enabled) {
		otgwl_drop(&vbus_lock);
		spin_unlock_irqrestore(&otgwl_spinlock, irqflags);
		return;
	}

	switch (event) {
	case USB_EVENT_VBUS:
	case USB_EVENT_ENUMERATED:
		otgwl_hold(&vbus_lock);
		break;

	case USB_EVENT_NONE:
	case USB_EVENT_ID:
	case USB_EVENT_CHARGER:
		otgwl_temporary_hold(&vbus_lock);
		break;

	default:
		break;
	}

	spin_unlock_irqrestore(&otgwl_spinlock, irqflags);
}

static int otgwl_otg_notifications(struct notifier_block *nb,
				   unsigned long event, void *unused)
{
	otgwl_handle_event(event);
	return NOTIFY_OK;
}

static int set_enabled(const char *val, const struct kernel_param *kp)
{
	int rv = param_set_bool(val, kp);

	if (rv)
		return rv;

	if (otgwl_xceiv)
		otgwl_handle_event(otgwl_xceiv->last_event);

	return 0;
}

static struct kernel_param_ops enabled_param_ops = {
	.set = set_enabled,
	.get = param_get_bool,
};

module_param_cb(enabled, &enabled_param_ops, &enabled, 0644);
MODULE_PARM_DESC(enabled, "enable wakelock when VBUS present");

static int __init otg_wakelock_init(void)
{
	int ret;
	struct usb_phy *phy;

	phy = usb_get_phy(USB_PHY_TYPE_USB2);

	if (IS_ERR(phy)) {
		pr_err("%s: No USB transceiver found\n", __func__);
		return PTR_ERR(phy);
	}
	otgwl_xceiv = phy;

	snprintf(vbus_lock.name, sizeof(vbus_lock.name), "vbus-%s",
		 dev_name(otgwl_xceiv->dev));
	wakeup_source_init(&vbus_lock.wakesrc, vbus_lock.name);

	otgwl_nb.notifier_call = otgwl_otg_notifications;
	ret = usb_register_notifier(otgwl_xceiv, &otgwl_nb);

	if (ret) {
		pr_err("%s: usb_register_notifier on transceiver %s"
		       " failed\n", __func__,
		       dev_name(otgwl_xceiv->dev));
		otgwl_xceiv = NULL;
		wakeup_source_trash(&vbus_lock.wakesrc);
		return ret;
	}

	otgwl_handle_event(otgwl_xceiv->last_event);
	return ret;
}

late_initcall(otg_wakelock_init);
