/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    yestice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    yestice, this list of conditions and the following disclaimer in
 *    distribution.
 *  * Neither the name Intel Corporation yesr the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#ifndef __iwl_yestif_wait_h__
#define __iwl_yestif_wait_h__

#include <linux/wait.h>

#include "iwl-trans.h"

struct iwl_yestif_wait_data {
	struct list_head yestif_waits;
	spinlock_t yestif_wait_lock;
	wait_queue_head_t yestif_waitq;
};

#define MAX_NOTIF_CMDS	5

/**
 * struct iwl_yestification_wait - yestification wait entry
 * @list: list head for global list
 * @fn: Function called with the yestification. If the function
 *	returns true, the wait is over, if it returns false then
 *	the waiter stays blocked. If yes function is given, any
 *	of the listed commands will unblock the waiter.
 * @cmds: command IDs
 * @n_cmds: number of command IDs
 * @triggered: waiter should be woken up
 * @aborted: wait was aborted
 *
 * This structure is yest used directly, to wait for a
 * yestification declare it on the stack, and call
 * iwl_init_yestification_wait() with appropriate
 * parameters. Then do whatever will cause the ucode
 * to yestify the driver, and to wait for that then
 * call iwl_wait_yestification().
 *
 * Each yestification is one-shot. If at some point we
 * need to support multi-shot yestifications (which
 * can't be allocated on the stack) we need to modify
 * the code for them.
 */
struct iwl_yestification_wait {
	struct list_head list;

	bool (*fn)(struct iwl_yestif_wait_data *yestif_data,
		   struct iwl_rx_packet *pkt, void *data);
	void *fn_data;

	u16 cmds[MAX_NOTIF_CMDS];
	u8 n_cmds;
	bool triggered, aborted;
};


/* caller functions */
void iwl_yestification_wait_init(struct iwl_yestif_wait_data *yestif_data);
bool iwl_yestification_wait(struct iwl_yestif_wait_data *yestif_data,
			   struct iwl_rx_packet *pkt);
void iwl_abort_yestification_waits(struct iwl_yestif_wait_data *yestif_data);

static inline void
iwl_yestification_yestify(struct iwl_yestif_wait_data *yestif_data)
{
	wake_up_all(&yestif_data->yestif_waitq);
}

static inline void
iwl_yestification_wait_yestify(struct iwl_yestif_wait_data *yestif_data,
			     struct iwl_rx_packet *pkt)
{
	if (iwl_yestification_wait(yestif_data, pkt))
		iwl_yestification_yestify(yestif_data);
}

/* user functions */
void __acquires(wait_entry)
iwl_init_yestification_wait(struct iwl_yestif_wait_data *yestif_data,
			   struct iwl_yestification_wait *wait_entry,
			   const u16 *cmds, int n_cmds,
			   bool (*fn)(struct iwl_yestif_wait_data *yestif_data,
				      struct iwl_rx_packet *pkt, void *data),
			   void *fn_data);

int __must_check __releases(wait_entry)
iwl_wait_yestification(struct iwl_yestif_wait_data *yestif_data,
		      struct iwl_yestification_wait *wait_entry,
		      unsigned long timeout);

void __releases(wait_entry)
iwl_remove_yestification(struct iwl_yestif_wait_data *yestif_data,
			struct iwl_yestification_wait *wait_entry);

#endif /* __iwl_yestif_wait_h__ */
