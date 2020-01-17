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
 *    the documentation and/or other materials provided with the
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
#include <linux/sched.h>
#include <linux/export.h>

#include "iwl-drv.h"
#include "yestif-wait.h"


void iwl_yestification_wait_init(struct iwl_yestif_wait_data *yestif_wait)
{
	spin_lock_init(&yestif_wait->yestif_wait_lock);
	INIT_LIST_HEAD(&yestif_wait->yestif_waits);
	init_waitqueue_head(&yestif_wait->yestif_waitq);
}
IWL_EXPORT_SYMBOL(iwl_yestification_wait_init);

bool iwl_yestification_wait(struct iwl_yestif_wait_data *yestif_wait,
			   struct iwl_rx_packet *pkt)
{
	bool triggered = false;

	if (!list_empty(&yestif_wait->yestif_waits)) {
		struct iwl_yestification_wait *w;

		spin_lock(&yestif_wait->yestif_wait_lock);
		list_for_each_entry(w, &yestif_wait->yestif_waits, list) {
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

			if (!w->fn || w->fn(yestif_wait, pkt, w->fn_data)) {
				w->triggered = true;
				triggered = true;
			}
		}
		spin_unlock(&yestif_wait->yestif_wait_lock);
	}

	return triggered;
}
IWL_EXPORT_SYMBOL(iwl_yestification_wait);

void iwl_abort_yestification_waits(struct iwl_yestif_wait_data *yestif_wait)
{
	struct iwl_yestification_wait *wait_entry;

	spin_lock(&yestif_wait->yestif_wait_lock);
	list_for_each_entry(wait_entry, &yestif_wait->yestif_waits, list)
		wait_entry->aborted = true;
	spin_unlock(&yestif_wait->yestif_wait_lock);

	wake_up_all(&yestif_wait->yestif_waitq);
}
IWL_EXPORT_SYMBOL(iwl_abort_yestification_waits);

void
iwl_init_yestification_wait(struct iwl_yestif_wait_data *yestif_wait,
			   struct iwl_yestification_wait *wait_entry,
			   const u16 *cmds, int n_cmds,
			   bool (*fn)(struct iwl_yestif_wait_data *yestif_wait,
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

	spin_lock_bh(&yestif_wait->yestif_wait_lock);
	list_add(&wait_entry->list, &yestif_wait->yestif_waits);
	spin_unlock_bh(&yestif_wait->yestif_wait_lock);
}
IWL_EXPORT_SYMBOL(iwl_init_yestification_wait);

void iwl_remove_yestification(struct iwl_yestif_wait_data *yestif_wait,
			     struct iwl_yestification_wait *wait_entry)
{
	spin_lock_bh(&yestif_wait->yestif_wait_lock);
	list_del(&wait_entry->list);
	spin_unlock_bh(&yestif_wait->yestif_wait_lock);
}
IWL_EXPORT_SYMBOL(iwl_remove_yestification);

int iwl_wait_yestification(struct iwl_yestif_wait_data *yestif_wait,
			  struct iwl_yestification_wait *wait_entry,
			  unsigned long timeout)
{
	int ret;

	ret = wait_event_timeout(yestif_wait->yestif_waitq,
				 wait_entry->triggered || wait_entry->aborted,
				 timeout);

	iwl_remove_yestification(yestif_wait, wait_entry);

	if (wait_entry->aborted)
		return -EIO;

	/* return value is always >= 0 */
	if (ret <= 0)
		return -ETIMEDOUT;
	return 0;
}
IWL_EXPORT_SYMBOL(iwl_wait_yestification);
