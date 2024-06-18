/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GUC_RELAY_TYPES_H_
#define _XE_GUC_RELAY_TYPES_H_

#include <linux/mempool.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

/**
 * struct xe_guc_relay - Data used by the VF-PF Relay Communication over GuC.
 */
struct xe_guc_relay {
	/**@lock: protects all internal data. */
	spinlock_t lock;

	/** @worker: dispatches incoming action messages. */
	struct work_struct worker;

	/** @pending_relays: list of sent requests that await a response. */
	struct list_head pending_relays;

	/** @incoming_actions: list of incoming relay action messages to process. */
	struct list_head incoming_actions;

	/** @pool: pool of the relay message buffers. */
	mempool_t pool;

	/** @last_rid: last Relay-ID used while sending a message. */
	u32 last_rid;
};

#endif
