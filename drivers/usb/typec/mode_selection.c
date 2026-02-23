// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC.
 */

#include <linux/types.h>
#include <linux/list_sort.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/usb/typec_altmode.h>

#include "class.h"

/**
 * struct mode_state - State tracking for a specific Type-C alternate mode
 * @svid: Standard or Vendor ID of the Alternate Mode
 * @priority: Mode priority
 * @error: Outcome of the last attempt to enter the mode
 * @list: List head to link this mode state into a prioritized list
 */
struct mode_state {
	u16 svid;
	u8 priority;
	int error;
	struct list_head list;
};

/**
 * struct mode_selection - Manages the selection and state of Alternate Modes
 * @mode_list: Prioritized list of available Alternate Modes
 * @lock: Mutex to protect mode_list
 * @work: Work structure
 * @partner: Handle to the Type-C partner device
 * @active_svid: svid of currently active mode
 * @timeout: Timeout for a mode entry attempt, ms
 * @delay: Delay between mode entry/exit attempts, ms
 */
struct mode_selection {
	struct list_head mode_list;
	/* Protects the mode_list*/
	struct mutex lock;
	struct delayed_work work;
	struct typec_partner *partner;
	u16 active_svid;
	unsigned int timeout;
	unsigned int delay;
};

/**
 * struct mode_order - Mode activation tracking
 * @svid: Standard or Vendor ID of the Alternate Mode
 * @enter: Flag indicating if the driver is currently attempting to enter or
 * exit the mode
 * @result: Outcome of the attempt to activate the mode
 */
struct mode_order {
	u16 svid;
	int enter;
	int result;
};

static int activate_altmode(struct device *dev, void *data)
{
	if (is_typec_partner_altmode(dev)) {
		struct typec_altmode *alt = to_typec_altmode(dev);
		struct mode_order *order = (struct mode_order *)data;

		if (order->svid == alt->svid) {
			if (alt->ops && alt->ops->activate)
				order->result = alt->ops->activate(alt, order->enter);
			else
				order->result = -EOPNOTSUPP;
			return 1;
		}
	}
	return 0;
}

static int mode_selection_activate(struct mode_selection *sel,
				   const u16 svid, const int enter)

	__must_hold(&sel->lock)
{
	struct mode_order order = {.svid = svid, .enter = enter, .result = -ENODEV};

	/*
	 * The port driver may acquire its internal mutex during alternate mode
	 * activation. Since this is the same mutex that may be held during the
	 * execution of typec_altmode_state_update(), it is crucial to release
	 * sel->mutex before activation to avoid potential deadlock.
	 * Note that sel->mode_list must remain invariant throughout this unlocked
	 * interval.
	 */
	mutex_unlock(&sel->lock);
	device_for_each_child(&sel->partner->dev, &order, activate_altmode);
	mutex_lock(&sel->lock);

	return order.result;
}

static void mode_list_clean(struct mode_selection *sel)
{
	struct mode_state *ms, *tmp;

	list_for_each_entry_safe(ms, tmp, &sel->mode_list, list) {
		list_del(&ms->list);
		kfree(ms);
	}
}

/**
 * mode_selection_work_fn() - Alternate mode activation task
 * @work: work structure
 *
 * - If the Alternate Mode currently prioritized at the top of the list is already
 * active, the entire selection process is considered finished.
 * - If a different Alternate Mode is currently active, the system must exit that
 * active mode first before attempting any new entry.
 *
 * The function then checks the result of the attempt to entre the current mode,
 * stored in the `ms->error` field:
 * - if the attempt FAILED, the mode is deactivated and removed from the list.
 * - `ms->error` value of 0 signifies that the mode has not yet been activated.
 *
 * Once successfully activated, the task is scheduled for subsequent entry after
 * a timeout period. The alternate mode driver is expected to call back with the
 * actual mode entry result via `typec_altmode_state_update()`.
 */
static void mode_selection_work_fn(struct work_struct *work)
{
	struct mode_selection *sel = container_of(work,
				struct mode_selection, work.work);
	struct mode_state *ms;
	unsigned int delay = sel->delay;
	int result;

	guard(mutex)(&sel->lock);

	ms = list_first_entry_or_null(&sel->mode_list, struct mode_state, list);
	if (!ms)
		return;

	if (sel->active_svid == ms->svid) {
		dev_dbg(&sel->partner->dev, "%x altmode is active\n", ms->svid);
		mode_list_clean(sel);
	} else if (sel->active_svid != 0) {
		result = mode_selection_activate(sel, sel->active_svid, 0);
		if (result)
			mode_list_clean(sel);
		else
			sel->active_svid = 0;
	} else if (ms->error) {
		dev_err(&sel->partner->dev, "%x: entry error %pe\n",
			ms->svid, ERR_PTR(ms->error));
		mode_selection_activate(sel, ms->svid, 0);
		list_del(&ms->list);
		kfree(ms);
	} else {
		result = mode_selection_activate(sel, ms->svid, 1);
		if (result) {
			dev_err(&sel->partner->dev, "%x: activation error %pe\n",
				ms->svid, ERR_PTR(result));
			list_del(&ms->list);
			kfree(ms);
		} else {
			delay = sel->timeout;
			ms->error = -ETIMEDOUT;
		}
	}

	if (!list_empty(&sel->mode_list))
		schedule_delayed_work(&sel->work, msecs_to_jiffies(delay));
}

void typec_altmode_state_update(struct typec_partner *partner, const u16 svid,
				const int error)
{
	struct mode_selection *sel = partner->sel;
	struct mode_state *ms;

	if (sel) {
		mutex_lock(&sel->lock);
		ms = list_first_entry_or_null(&sel->mode_list, struct mode_state, list);
		if (ms && ms->svid == svid) {
			ms->error = error;
			if (cancel_delayed_work(&sel->work))
				schedule_delayed_work(&sel->work, 0);
		}
		if (!error)
			sel->active_svid = svid;
		else
			sel->active_svid = 0;
		mutex_unlock(&sel->lock);
	}
}
EXPORT_SYMBOL_GPL(typec_altmode_state_update);

static int compare_priorities(void *priv,
			      const struct list_head *a, const struct list_head *b)
{
	const struct mode_state *msa = container_of(a, struct mode_state, list);
	const struct mode_state *msb = container_of(b, struct mode_state, list);

	if (msa->priority < msb->priority)
		return -1;
	return 1;
}

static int altmode_add_to_list(struct device *dev, void *data)
{
	if (is_typec_partner_altmode(dev)) {
		struct list_head *list = (struct list_head *)data;
		struct typec_altmode *altmode = to_typec_altmode(dev);
		const struct typec_altmode *pdev = typec_altmode_get_partner(altmode);
		struct mode_state *ms;

		if (pdev && altmode->ops && altmode->ops->activate) {
			ms = kzalloc_obj(*ms);
			if (!ms)
				return -ENOMEM;
			ms->svid = pdev->svid;
			ms->priority = pdev->priority;
			INIT_LIST_HEAD(&ms->list);
			list_add_tail(&ms->list, list);
		}
	}
	return 0;
}

int typec_mode_selection_start(struct typec_partner *partner,
			       const unsigned int delay, const unsigned int timeout)
{
	struct mode_selection *sel;
	int ret;

	if (partner->usb_mode == USB_MODE_USB4)
		return -EBUSY;

	if (partner->sel)
		return -EALREADY;

	sel = kzalloc_obj(*sel);
	if (!sel)
		return -ENOMEM;

	INIT_LIST_HEAD(&sel->mode_list);

	ret = device_for_each_child(&partner->dev, &sel->mode_list,
				    altmode_add_to_list);

	if (ret || list_empty(&sel->mode_list)) {
		mode_list_clean(sel);
		kfree(sel);
		return ret;
	}

	list_sort(NULL, &sel->mode_list, compare_priorities);
	sel->partner = partner;
	sel->delay = delay;
	sel->timeout = timeout;
	mutex_init(&sel->lock);
	INIT_DELAYED_WORK(&sel->work, mode_selection_work_fn);
	schedule_delayed_work(&sel->work, msecs_to_jiffies(delay));
	partner->sel = sel;

	return 0;
}
EXPORT_SYMBOL_GPL(typec_mode_selection_start);

void typec_mode_selection_delete(struct typec_partner *partner)
{
	struct mode_selection *sel = partner->sel;

	if (sel) {
		partner->sel = NULL;
		cancel_delayed_work_sync(&sel->work);
		mode_list_clean(sel);
		mutex_destroy(&sel->lock);
		kfree(sel);
	}
}
EXPORT_SYMBOL_GPL(typec_mode_selection_delete);
