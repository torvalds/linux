/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2014 Intel Corporation
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_notif_wait_h__
#define __iwl_notif_wait_h__

#include <linux/wait.h>

#include "iwl-trans.h"

struct iwl_notif_wait_data {
	struct list_head notif_waits;
	spinlock_t notif_wait_lock;
	wait_queue_head_t notif_waitq;
};

#define MAX_NOTIF_CMDS	5

/**
 * struct iwl_notification_wait - notification wait entry
 * @list: list head for global list
 * @fn: Function called with the notification. If the function
 *	returns true, the wait is over, if it returns false then
 *	the waiter stays blocked. If no function is given, any
 *	of the listed commands will unblock the waiter.
 * @cmds: command IDs
 * @n_cmds: number of command IDs
 * @triggered: waiter should be woken up
 * @aborted: wait was aborted
 *
 * This structure is not used directly, to wait for a
 * notification declare it on the stack, and call
 * iwl_init_notification_wait() with appropriate
 * parameters. Then do whatever will cause the ucode
 * to notify the driver, and to wait for that then
 * call iwl_wait_notification().
 *
 * Each notification is one-shot. If at some point we
 * need to support multi-shot notifications (which
 * can't be allocated on the stack) we need to modify
 * the code for them.
 */
struct iwl_notification_wait {
	struct list_head list;

	bool (*fn)(struct iwl_notif_wait_data *notif_data,
		   struct iwl_rx_packet *pkt, void *data);
	void *fn_data;

	u16 cmds[MAX_NOTIF_CMDS];
	u8 n_cmds;
	bool triggered, aborted;
};


/* caller functions */
void iwl_notification_wait_init(struct iwl_notif_wait_data *notif_data);
bool iwl_notification_wait(struct iwl_notif_wait_data *notif_data,
			   struct iwl_rx_packet *pkt);
void iwl_abort_notification_waits(struct iwl_notif_wait_data *notif_data);

static inline void
iwl_notification_notify(struct iwl_notif_wait_data *notif_data)
{
	wake_up_all(&notif_data->notif_waitq);
}

static inline void
iwl_notification_wait_notify(struct iwl_notif_wait_data *notif_data,
			     struct iwl_rx_packet *pkt)
{
	if (iwl_notification_wait(notif_data, pkt))
		iwl_notification_notify(notif_data);
}

/* user functions */
void __acquires(wait_entry)
iwl_init_notification_wait(struct iwl_notif_wait_data *notif_data,
			   struct iwl_notification_wait *wait_entry,
			   const u16 *cmds, int n_cmds,
			   bool (*fn)(struct iwl_notif_wait_data *notif_data,
				      struct iwl_rx_packet *pkt, void *data),
			   void *fn_data);

int __must_check __releases(wait_entry)
iwl_wait_notification(struct iwl_notif_wait_data *notif_data,
		      struct iwl_notification_wait *wait_entry,
		      unsigned long timeout);

void __releases(wait_entry)
iwl_remove_notification(struct iwl_notif_wait_data *notif_data,
			struct iwl_notification_wait *wait_entry);

#endif /* __iwl_notif_wait_h__ */
