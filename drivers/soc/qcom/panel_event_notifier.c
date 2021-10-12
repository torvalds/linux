// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/soc/qcom/panel_event_notifier.h>

struct panel_event_notifier_entry {
	panel_event_notifier_handler handler;
	void *pvt_data;
	struct drm_panel *panel;
	enum panel_event_notifier_tag tag;
};

static DEFINE_MUTEX(panel_event_notifier_entries_lock);
static struct panel_event_notifier_entry
		panel_event_notifier_entries[PANEL_EVENT_NOTIFIER_CLIENT_MAX];

static bool panel_event_notifier_tag_valid(enum panel_event_notifier_tag tag)
{
	return tag > PANEL_EVENT_NOTIFICATION_NONE &&
			tag < PANEL_EVENT_NOTIFICATION_MAX;
}

/**
 * panel_event_notifier_register: responsible for registering clients
 *                                 interested in panel event notifications.
 *                                 clients register with a client handle and tag
 *                                 suggesting the notifications they are
 *                                 interested in and a callback which is
 *                                 triggered when the interested panel
 *                                 notifications are received.
 *
 * @tag: The tag for which the caller would like to receive panel events for.
 *
 * @client_handle: handle to recongnize the client registering for
 *                 notifications.
 *
 * @panel: struct drm_panel for which the panel events are requested for.
 *
 * @handler: The handler that will be invoked when a panel event notification is
 *           received pertaining to @tag. The handler will be invoked with a
 *           pointer to private data registered by the client that is needed
 *           for servicing the notification.
 *
 * @pvt_data: The data that should be passed to @handler when a notification
 *            occurs
 *
 * On success, the function will return a cookie.
 *
 */
void *panel_event_notifier_register(enum panel_event_notifier_tag tag,
		enum panel_event_notifier_client client_handle,
		struct drm_panel *panel,
			panel_event_notifier_handler handler, void *pvt_data)
{
	struct panel_event_notifier_entry *entry;

	if (!panel_event_notifier_tag_valid(tag) || !handler) {
		pr_err("Invalid tag or handler found while registering\n");
		return ERR_PTR(-EINVAL);
	}

	if (client_handle < 0 ||
			client_handle >= PANEL_EVENT_NOTIFIER_CLIENT_MAX) {
		pr_err("Invalid client handle used for registering\n");
		return ERR_PTR(-EINVAL);
	}

	mutex_lock(&panel_event_notifier_entries_lock);
	entry = &panel_event_notifier_entries[client_handle];
	if (entry->handler) {
		mutex_unlock(&panel_event_notifier_entries_lock);
		return ERR_PTR(-EEXIST);
	}
	entry->panel = panel;
	entry->handler = handler;
	entry->pvt_data = pvt_data;
	entry->tag = tag;
	mutex_unlock(&panel_event_notifier_entries_lock);

	pr_debug("client %d registered successfully\n", client_handle);
	return entry;
}
EXPORT_SYMBOL(panel_event_notifier_register);

/**
 * panel_event_notifier_unregister: responsible for unregistering clients.
 *
 * @cookie: cookie used for unregistering client.
 */
void panel_event_notifier_unregister(void *cookie)
{
	struct panel_event_notifier_entry *entry = cookie;

	if (!cookie)
		return;

	mutex_lock(&panel_event_notifier_entries_lock);
	entry->panel = NULL;
	entry->handler = NULL;
	entry->pvt_data = NULL;
	entry->tag = PANEL_EVENT_NOTIFICATION_NONE;
	mutex_unlock(&panel_event_notifier_entries_lock);
}
EXPORT_SYMBOL(panel_event_notifier_unregister);

/**
 * panel_event_notifion_trigger: responsible for triggering notifications.
 *                               Contains tag which notifies the panel
 *                               notification is on premiary or secondary
 *                               display and a notifcation carrying necessary
 *                               data for clients to consume while servicing the
 *                               notification. A handler registered by the
 *                               client will be triggered to notify the event.
 *
 * @tag: tag suggesting the panel on which notification is triggered whether
 *I      primary or secondary display panel.
 *
 * @notification: contains data required for client to address the notification.
 */
void panel_event_notification_trigger(enum panel_event_notifier_tag tag,
		 struct panel_event_notification *notification)
{
	struct panel_event_notifier_entry *entry;
	panel_event_notifier_handler handler = NULL;
	void *pvt_data;
	int i;

	if (!panel_event_notifier_tag_valid(tag)) {
		pr_err("Invalid panel notifier tag\n");
		return;
	}

	for (i = 0; i < PANEL_EVENT_NOTIFIER_CLIENT_MAX; i++) {
		mutex_lock(&panel_event_notifier_entries_lock);
		entry = &panel_event_notifier_entries[i];
		if (notification->panel != entry->panel) {
			pr_debug("invalid panel found notification_panel:0x%x entry_panel:0x%x\n",
					notification->panel, entry->panel);
			mutex_unlock(&panel_event_notifier_entries_lock);
			continue;
		}

		/* skip client entries not subscribed to tag */
		if (entry->tag != tag) {
			pr_err("tag mismatch entry->tag:%d tag:%d\n", entry->tag, tag);
			mutex_unlock(&panel_event_notifier_entries_lock);
			continue;
		}

		handler = entry->handler;
		pvt_data = entry->pvt_data;
		mutex_unlock(&panel_event_notifier_entries_lock);

		pr_debug("triggering notification for tag:%d, type:%d\n",
				tag, notification->notif_type);

		if (handler)
			handler(tag, notification, pvt_data);

	}
}
EXPORT_SYMBOL(panel_event_notification_trigger);

static int __init panel_event_notifier_init(void)
{
	pr_debug("Panel event notifier initialized\n");
	return 0;
}
module_init(panel_event_notifier_init);

static void __exit panel_event_notifier_exit(void)
{
	pr_debug("Panel event notifier exited\n");
}
module_exit(panel_event_notifier_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Panel event notifier");
