/* Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * Subsystem Notifier -- Provides notifications
 * of subsys events.
 *
 * Use subsys_notif_register_notifier to register for notifications
 * and subsys_notif_queue_notification to send notifications.
 *
 */

#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/stringify.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <mach/subsystem_notif.h>

struct subsys_notif_info {
	char name[50];
	struct srcu_notifier_head subsys_notif_rcvr_list;
	struct list_head list;
};

static LIST_HEAD(subsystem_list);
static DEFINE_MUTEX(notif_lock);
static DEFINE_MUTEX(notif_add_lock);

#if defined(SUBSYS_RESTART_DEBUG)
static void subsys_notif_reg_test_notifier(const char *);
#endif

static struct subsys_notif_info *_notif_find_subsys(const char *subsys_name)
{
	struct subsys_notif_info *subsys;

	mutex_lock(&notif_lock);
	list_for_each_entry(subsys, &subsystem_list, list)
		if (!strncmp(subsys->name, subsys_name,
				ARRAY_SIZE(subsys->name))) {
			mutex_unlock(&notif_lock);
			return subsys;
		}
	mutex_unlock(&notif_lock);

	return NULL;
}

void *subsys_notif_register_notifier(
			const char *subsys_name, struct notifier_block *nb)
{
	int ret;
	struct subsys_notif_info *subsys = _notif_find_subsys(subsys_name);

	if (!subsys) {

		/* Possible first time reference to this subsystem. Add it. */
		subsys = (struct subsys_notif_info *)
				subsys_notif_add_subsys(subsys_name);

		if (!subsys)
			return ERR_PTR(-EINVAL);
	}

	ret = srcu_notifier_chain_register(
		&subsys->subsys_notif_rcvr_list, nb);

	if (ret < 0)
		return ERR_PTR(ret);

	return subsys;
}
EXPORT_SYMBOL(subsys_notif_register_notifier);

int subsys_notif_unregister_notifier(void *subsys_handle,
				struct notifier_block *nb)
{
	int ret;
	struct subsys_notif_info *subsys =
			(struct subsys_notif_info *)subsys_handle;

	if (!subsys)
		return -EINVAL;

	ret = srcu_notifier_chain_unregister(
		&subsys->subsys_notif_rcvr_list, nb);

	return ret;
}
EXPORT_SYMBOL(subsys_notif_unregister_notifier);

void *subsys_notif_add_subsys(const char *subsys_name)
{
	struct subsys_notif_info *subsys = NULL;

	if (!subsys_name)
		goto done;

	mutex_lock(&notif_add_lock);

	subsys = _notif_find_subsys(subsys_name);

	if (subsys) {
		mutex_unlock(&notif_add_lock);
		goto done;
	}

	subsys = kmalloc(sizeof(struct subsys_notif_info), GFP_KERNEL);

	if (!subsys) {
		mutex_unlock(&notif_add_lock);
		return ERR_PTR(-EINVAL);
	}

	strlcpy(subsys->name, subsys_name, ARRAY_SIZE(subsys->name));

	srcu_init_notifier_head(&subsys->subsys_notif_rcvr_list);

	INIT_LIST_HEAD(&subsys->list);

	mutex_lock(&notif_lock);
	list_add_tail(&subsys->list, &subsystem_list);
	mutex_unlock(&notif_lock);

	#if defined(SUBSYS_RESTART_DEBUG)
	subsys_notif_reg_test_notifier(subsys->name);
	#endif

	mutex_unlock(&notif_add_lock);

done:
	return subsys;
}
EXPORT_SYMBOL(subsys_notif_add_subsys);

int subsys_notif_queue_notification(void *subsys_handle,
					enum subsys_notif_type notif_type)
{
	int ret = 0;
	struct subsys_notif_info *subsys =
		(struct subsys_notif_info *) subsys_handle;

	if (!subsys)
		return -EINVAL;

	if (notif_type < 0 || notif_type >= SUBSYS_NOTIF_TYPE_COUNT)
		return -EINVAL;

	ret = srcu_notifier_call_chain(
		&subsys->subsys_notif_rcvr_list, notif_type,
		(void *)subsys);

	return ret;
}
EXPORT_SYMBOL(subsys_notif_queue_notification);

#if defined(SUBSYS_RESTART_DEBUG)
static const char *notif_to_string(enum subsys_notif_type notif_type)
{
	switch (notif_type) {

	case	SUBSYS_BEFORE_SHUTDOWN:
		return __stringify(SUBSYS_BEFORE_SHUTDOWN);

	case	SUBSYS_AFTER_SHUTDOWN:
		return __stringify(SUBSYS_AFTER_SHUTDOWN);

	case	SUBSYS_BEFORE_POWERUP:
		return __stringify(SUBSYS_BEFORE_POWERUP);

	case	SUBSYS_AFTER_POWERUP:
		return __stringify(SUBSYS_AFTER_POWERUP);

	default:
		return "unknown";
	}
}

static int subsys_notifier_test_call(struct notifier_block *this,
				  unsigned long code,
				  void *data)
{
	switch (code) {

	default:
		printk(KERN_WARNING "%s: Notification %s from subsystem %p\n",
			__func__, notif_to_string(code), data);
	break;

	}

	return NOTIFY_DONE;
}

static struct notifier_block nb = {
	.notifier_call = subsys_notifier_test_call,
};

static void subsys_notif_reg_test_notifier(const char *subsys_name)
{
	void *handle = subsys_notif_register_notifier(subsys_name, &nb);
	printk(KERN_WARNING "%s: Registered test notifier, handle=%p",
			__func__, handle);
}
#endif

MODULE_DESCRIPTION("Subsystem Restart Notifier");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");
