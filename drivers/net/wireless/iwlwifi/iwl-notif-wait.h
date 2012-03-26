/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2012 Intel Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2012 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
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
#ifndef __iwl_notif_wait_h__
#define __iwl_notif_wait_h__

#include <linux/wait.h>

#include "iwl-trans.h"

struct iwl_notif_wait_data {
	struct list_head notif_waits;
	spinlock_t notif_wait_lock;
	wait_queue_head_t notif_waitq;
};

/**
 * struct iwl_notification_wait - notification wait entry
 * @list: list head for global list
 * @fn: function called with the notification
 * @cmd: command ID
 *
 * This structure is not used directly, to wait for a
 * notification declare it on the stack, and call
 * iwlagn_init_notification_wait() with appropriate
 * parameters. Then do whatever will cause the ucode
 * to notify the driver, and to wait for that then
 * call iwlagn_wait_notification().
 *
 * Each notification is one-shot. If at some point we
 * need to support multi-shot notifications (which
 * can't be allocated on the stack) we need to modify
 * the code for them.
 */
struct iwl_notification_wait {
	struct list_head list;

	void (*fn)(struct iwl_notif_wait_data *notif_data,
		   struct iwl_rx_packet *pkt, void *data);
	void *fn_data;

	u8 cmd;
	bool triggered, aborted;
};


/* caller functions */
void iwl_notification_wait_init(struct iwl_notif_wait_data *notif_data);
void iwl_notification_wait_notify(struct iwl_notif_wait_data *notif_data,
				  struct iwl_rx_packet *pkt);
void iwl_abort_notification_waits(struct iwl_notif_wait_data *notif_data);

/* user functions */
void __acquires(wait_entry)
iwl_init_notification_wait(struct iwl_notif_wait_data *notif_data,
			   struct iwl_notification_wait *wait_entry,
			   u8 cmd,
			   void (*fn)(struct iwl_notif_wait_data *notif_data,
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
