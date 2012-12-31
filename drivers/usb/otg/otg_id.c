/*
 * Copyright (C) 2011 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
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
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/usb/otg_id.h>

static DEFINE_MUTEX(otg_id_lock);
static struct plist_head otg_id_plist =
	PLIST_HEAD_INIT(otg_id_plist);
static struct otg_id_notifier_block *otg_id_active;
static bool otg_id_cancelling;
static bool otg_id_inited;
static int otg_id_suspended;
static bool otg_id_pending;

static void otg_id_cancel(void)
{
	if (otg_id_active) {
		otg_id_cancelling = true;
		mutex_unlock(&otg_id_lock);

		otg_id_active->cancel(otg_id_active);

		mutex_lock(&otg_id_lock);
		otg_id_cancelling = false;
	}
}

static void __otg_id_notify(void)
{
	int ret;
	struct otg_id_notifier_block *otg_id_nb;
	bool proxy_wait = false;
	if (plist_head_empty(&otg_id_plist))
		return;

	plist_for_each_entry(otg_id_nb, &otg_id_plist, p) {
		if (proxy_wait) {
			if (otg_id_nb->proxy_wait)
				ret = otg_id_nb->proxy_wait(otg_id_nb);
		} else {
			ret = otg_id_nb->detect(otg_id_nb);
		}
		if (ret == OTG_ID_HANDLED) {
			otg_id_active = otg_id_nb;
			return;
		}
		if (ret == OTG_ID_PROXY_WAIT)
			proxy_wait = true;

	}

	WARN(1, "otg id event not handled");
	otg_id_active = NULL;
}

int otg_id_init(void)
{
	mutex_lock(&otg_id_lock);

	otg_id_inited = true;
	__otg_id_notify();

	mutex_unlock(&otg_id_lock);
	return 0;
}
late_initcall(otg_id_init);

/**
 * otg_id_register_notifier
 * @otg_id_nb: notifier block containing priority and callback function
 *
 * Register a notifier that will be called on any USB cable state change.
 * The priority determines the order the callback will be called in, a higher
 * number will be called first.  A callback function needs to determine the
 * type of USB cable that is connected.  If it can determine the type, it
 * should notify the appropriate drivers (for example, call an otg notifier
 * with USB_EVENT_VBUS), and return OTG_ID_HANDLED.  Once a callback has
 * returned OTG_ID_HANDLED, it is responsible for calling otg_id_notify() when
 * the detected USB cable is disconnected.
 */
int otg_id_register_notifier(struct otg_id_notifier_block *otg_id_nb)
{
	plist_node_init(&otg_id_nb->p, otg_id_nb->priority);

	mutex_lock(&otg_id_lock);
	plist_add(&otg_id_nb->p, &otg_id_plist);

	if (otg_id_inited) {
		otg_id_cancel();
		__otg_id_notify();
	}

	mutex_unlock(&otg_id_lock);

	return 0;
}

void otg_id_unregister_notifier(struct otg_id_notifier_block *otg_id_nb)
{
	mutex_lock(&otg_id_lock);

	plist_del(&otg_id_nb->p, &otg_id_plist);

	if (otg_id_inited && (otg_id_active == otg_id_nb)) {
		otg_id_cancel();
		__otg_id_notify();
	}

	mutex_unlock(&otg_id_lock);
}

/**
 * otg_id_notify
 *
 * Notify listeners on any USB cable state change.
 *
 * A driver may only call otg_id_notify if it returned OTG_ID_HANDLED the last
 * time it's notifier was called, and it's cancel function has not been called.
 */
void otg_id_notify(void)
{
	mutex_lock(&otg_id_lock);

	if (otg_id_cancelling)
		goto out;

	if (otg_id_suspended != 0) {
		otg_id_pending = true;
		goto out;
	}

	__otg_id_notify();
out:
	mutex_unlock(&otg_id_lock);
}

/**
 * otg_id_suspend
 *
 * Mark the otg_id subsystem as going into suspend. From here on out,
 * any notifications will be deferred until the last otg_id client resumes.
 * If there is a pending notification when calling this function, it will
 * return a negative errno and expects that the caller will abort suspend.
 * Returs 0 on success.
 */
int otg_id_suspend(void)
{
	int ret = 0;

	mutex_lock(&otg_id_lock);

	/*
	 * if there's a pending notification, tell the caller to abort suspend
	 */
	if (otg_id_suspended != 0 && otg_id_pending) {
		pr_info("otg_id: pending notification, should abort suspend\n");
		ret = -EBUSY;
		goto out;
	}

	otg_id_suspended++;
out:
	mutex_unlock(&otg_id_lock);
	return ret;
}

/**
 * otg_id_resume
 *
 * Inform the otg_id subsystem that a client is resuming. If this is the
 * last client to be resumed and there's a pending notification,
 * otg_id_notify() is called.
 */
void otg_id_resume(void)
{
	mutex_lock(&otg_id_lock);
	if (WARN(!otg_id_suspended, "unbalanced otg_id_resume\n"))
		goto out;
	if (--otg_id_suspended == 0) {
		if (otg_id_pending) {
			pr_info("otg_id: had pending notification\n");
			otg_id_pending = false;
			__otg_id_notify();
		}
	}
out:
	mutex_unlock(&otg_id_lock);
}
