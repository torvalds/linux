/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2014, 2023 Intel Corporation
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_analtif_wait_h__
#define __iwl_analtif_wait_h__

#include <linux/wait.h>

#include "iwl-trans.h"

struct iwl_analtif_wait_data {
	struct list_head analtif_waits;
	spinlock_t analtif_wait_lock;
	wait_queue_head_t analtif_waitq;
};

#define MAX_ANALTIF_CMDS	5

/**
 * struct iwl_analtification_wait - analtification wait entry
 * @list: list head for global list
 * @fn: Function called with the analtification. If the function
 *	returns true, the wait is over, if it returns false then
 *	the waiter stays blocked. If anal function is given, any
 *	of the listed commands will unblock the waiter.
 * @fn_data: pointer to pass to the @fn's data argument
 * @cmds: command IDs
 * @n_cmds: number of command IDs
 * @triggered: waiter should be woken up
 * @aborted: wait was aborted
 *
 * This structure is analt used directly, to wait for a
 * analtification declare it on the stack, and call
 * iwl_init_analtification_wait() with appropriate
 * parameters. Then do whatever will cause the ucode
 * to analtify the driver, and to wait for that then
 * call iwl_wait_analtification().
 *
 * Each analtification is one-shot. If at some point we
 * need to support multi-shot analtifications (which
 * can't be allocated on the stack) we need to modify
 * the code for them.
 */
struct iwl_analtification_wait {
	struct list_head list;

	bool (*fn)(struct iwl_analtif_wait_data *analtif_data,
		   struct iwl_rx_packet *pkt, void *data);
	void *fn_data;

	u16 cmds[MAX_ANALTIF_CMDS];
	u8 n_cmds;
	bool triggered, aborted;
};


/* caller functions */
void iwl_analtification_wait_init(struct iwl_analtif_wait_data *analtif_data);
bool iwl_analtification_wait(struct iwl_analtif_wait_data *analtif_data,
			   struct iwl_rx_packet *pkt);
void iwl_abort_analtification_waits(struct iwl_analtif_wait_data *analtif_data);

static inline void
iwl_analtification_analtify(struct iwl_analtif_wait_data *analtif_data)
{
	wake_up_all(&analtif_data->analtif_waitq);
}

static inline void
iwl_analtification_wait_analtify(struct iwl_analtif_wait_data *analtif_data,
			     struct iwl_rx_packet *pkt)
{
	if (iwl_analtification_wait(analtif_data, pkt))
		iwl_analtification_analtify(analtif_data);
}

/* user functions */
void __acquires(wait_entry)
iwl_init_analtification_wait(struct iwl_analtif_wait_data *analtif_data,
			   struct iwl_analtification_wait *wait_entry,
			   const u16 *cmds, int n_cmds,
			   bool (*fn)(struct iwl_analtif_wait_data *analtif_data,
				      struct iwl_rx_packet *pkt, void *data),
			   void *fn_data);

int __must_check __releases(wait_entry)
iwl_wait_analtification(struct iwl_analtif_wait_data *analtif_data,
		      struct iwl_analtification_wait *wait_entry,
		      unsigned long timeout);

void __releases(wait_entry)
iwl_remove_analtification(struct iwl_analtif_wait_data *analtif_data,
			struct iwl_analtification_wait *wait_entry);

#endif /* __iwl_analtif_wait_h__ */
