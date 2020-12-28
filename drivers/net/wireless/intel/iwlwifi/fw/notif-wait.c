// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2005-2014 Intel Corporation
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 */
#include <linux/sched.h>
#include <linux/export.h>

#include "iwl-drv.h"
#include "notif-wait.h"


void iwl_notification_wait_init(struct iwl_notif_wait_data *notif_wait)
{
	spin_lock_init(&notif_wait->notif_wait_lock);
	INIT_LIST_HEAD(&notif_wait->notif_waits);
	init_waitqueue_head(&notif_wait->notif_waitq);
}
IWL_EXPORT_SYMBOL(iwl_notification_wait_init);

bool iwl_notification_wait(struct iwl_notif_wait_data *notif_wait,
			   struct iwl_rx_packet *pkt)
{
	bool triggered = false;

	if (!list_empty(&notif_wait->notif_waits)) {
		struct iwl_notification_wait *w;

		spin_lock(&notif_wait->notif_wait_lock);
		list_for_each_entry(w, &notif_wait->notif_waits, list) {
			int i;
			bool found = false;

			/*
			 * If it already finished (triggered) or has been
			 * aborted then don't evaluate it again to avoid races,
			 * Otherwise the function could be called again even
			 * though it returned true before
			 */
			if (w->triggered || w->aborted)
				continue;

			for (i = 0; i < w->n_cmds; i++) {
				u16 rec_id = WIDE_ID(pkt->hdr.group_id,
						     pkt->hdr.cmd);

				if (w->cmds[i] == rec_id ||
				    (!iwl_cmd_groupid(w->cmds[i]) &&
				     DEF_ID(w->cmds[i]) == rec_id)) {
					found = true;
					break;
				}
			}
			if (!found)
				continue;

			if (!w->fn || w->fn(notif_wait, pkt, w->fn_data)) {
				w->triggered = true;
				triggered = true;
			}
		}
		spin_unlock(&notif_wait->notif_wait_lock);
	}

	return triggered;
}
IWL_EXPORT_SYMBOL(iwl_notification_wait);

void iwl_abort_notification_waits(struct iwl_notif_wait_data *notif_wait)
{
	struct iwl_notification_wait *wait_entry;

	spin_lock(&notif_wait->notif_wait_lock);
	list_for_each_entry(wait_entry, &notif_wait->notif_waits, list)
		wait_entry->aborted = true;
	spin_unlock(&notif_wait->notif_wait_lock);

	wake_up_all(&notif_wait->notif_waitq);
}
IWL_EXPORT_SYMBOL(iwl_abort_notification_waits);

void
iwl_init_notification_wait(struct iwl_notif_wait_data *notif_wait,
			   struct iwl_notification_wait *wait_entry,
			   const u16 *cmds, int n_cmds,
			   bool (*fn)(struct iwl_notif_wait_data *notif_wait,
				      struct iwl_rx_packet *pkt, void *data),
			   void *fn_data)
{
	if (WARN_ON(n_cmds > MAX_NOTIF_CMDS))
		n_cmds = MAX_NOTIF_CMDS;

	wait_entry->fn = fn;
	wait_entry->fn_data = fn_data;
	wait_entry->n_cmds = n_cmds;
	memcpy(wait_entry->cmds, cmds, n_cmds * sizeof(u16));
	wait_entry->triggered = false;
	wait_entry->aborted = false;

	spin_lock_bh(&notif_wait->notif_wait_lock);
	list_add(&wait_entry->list, &notif_wait->notif_waits);
	spin_unlock_bh(&notif_wait->notif_wait_lock);
}
IWL_EXPORT_SYMBOL(iwl_init_notification_wait);

void iwl_remove_notification(struct iwl_notif_wait_data *notif_wait,
			     struct iwl_notification_wait *wait_entry)
{
	spin_lock_bh(&notif_wait->notif_wait_lock);
	list_del(&wait_entry->list);
	spin_unlock_bh(&notif_wait->notif_wait_lock);
}
IWL_EXPORT_SYMBOL(iwl_remove_notification);

int iwl_wait_notification(struct iwl_notif_wait_data *notif_wait,
			  struct iwl_notification_wait *wait_entry,
			  unsigned long timeout)
{
	int ret;

	ret = wait_event_timeout(notif_wait->notif_waitq,
				 wait_entry->triggered || wait_entry->aborted,
				 timeout);

	iwl_remove_notification(notif_wait, wait_entry);

	if (wait_entry->aborted)
		return -EIO;

	/* return value is always >= 0 */
	if (ret <= 0)
		return -ETIMEDOUT;
	return 0;
}
IWL_EXPORT_SYMBOL(iwl_wait_notification);
