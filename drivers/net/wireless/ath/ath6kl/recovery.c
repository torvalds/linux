/*
 * Copyright (c) 2012 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "core.h"
#include "cfg80211.h"
#include "debug.h"

static void ath6kl_recovery_work(struct work_struct *work)
{
	struct ath6kl *ar = container_of(work, struct ath6kl,
					 fw_recovery.recovery_work);

	ar->state = ATH6KL_STATE_RECOVERY;

	timer_delete_sync(&ar->fw_recovery.hb_timer);

	ath6kl_init_hw_restart(ar);

	ar->state = ATH6KL_STATE_ON;
	clear_bit(WMI_CTRL_EP_FULL, &ar->flag);

	ar->fw_recovery.err_reason = 0;

	if (ar->fw_recovery.hb_poll)
		mod_timer(&ar->fw_recovery.hb_timer, jiffies +
			  msecs_to_jiffies(ar->fw_recovery.hb_poll));
}

void ath6kl_recovery_err_notify(struct ath6kl *ar, enum ath6kl_fw_err reason)
{
	if (!ar->fw_recovery.enable)
		return;

	ath6kl_dbg(ATH6KL_DBG_RECOVERY, "Fw error detected, reason:%d\n",
		   reason);

	set_bit(reason, &ar->fw_recovery.err_reason);

	if (!test_bit(RECOVERY_CLEANUP, &ar->flag) &&
	    ar->state != ATH6KL_STATE_RECOVERY)
		queue_work(ar->ath6kl_wq, &ar->fw_recovery.recovery_work);
}

void ath6kl_recovery_hb_event(struct ath6kl *ar, u32 cookie)
{
	if (cookie == ar->fw_recovery.seq_num)
		ar->fw_recovery.hb_pending = false;
}

static void ath6kl_recovery_hb_timer(struct timer_list *t)
{
	struct ath6kl *ar = timer_container_of(ar, t, fw_recovery.hb_timer);
	int err;

	if (test_bit(RECOVERY_CLEANUP, &ar->flag) ||
	    (ar->state == ATH6KL_STATE_RECOVERY))
		return;

	if (ar->fw_recovery.hb_pending)
		ar->fw_recovery.hb_misscnt++;
	else
		ar->fw_recovery.hb_misscnt = 0;

	if (ar->fw_recovery.hb_misscnt > ATH6KL_HB_RESP_MISS_THRES) {
		ar->fw_recovery.hb_misscnt = 0;
		ar->fw_recovery.seq_num = 0;
		ar->fw_recovery.hb_pending = false;
		ath6kl_recovery_err_notify(ar, ATH6KL_FW_HB_RESP_FAILURE);
		return;
	}

	ar->fw_recovery.seq_num++;
	ar->fw_recovery.hb_pending = true;

	err = ath6kl_wmi_get_challenge_resp_cmd(ar->wmi,
						ar->fw_recovery.seq_num, 0);
	if (err)
		ath6kl_warn("Failed to send hb challenge request, err:%d\n",
			    err);

	mod_timer(&ar->fw_recovery.hb_timer, jiffies +
		  msecs_to_jiffies(ar->fw_recovery.hb_poll));
}

void ath6kl_recovery_init(struct ath6kl *ar)
{
	struct ath6kl_fw_recovery *recovery = &ar->fw_recovery;

	clear_bit(RECOVERY_CLEANUP, &ar->flag);
	INIT_WORK(&recovery->recovery_work, ath6kl_recovery_work);
	recovery->seq_num = 0;
	recovery->hb_misscnt = 0;
	ar->fw_recovery.hb_pending = false;
	timer_setup(&ar->fw_recovery.hb_timer, ath6kl_recovery_hb_timer,
		    TIMER_DEFERRABLE);

	if (ar->fw_recovery.hb_poll)
		mod_timer(&ar->fw_recovery.hb_timer, jiffies +
			  msecs_to_jiffies(ar->fw_recovery.hb_poll));
}

void ath6kl_recovery_cleanup(struct ath6kl *ar)
{
	if (!ar->fw_recovery.enable)
		return;

	set_bit(RECOVERY_CLEANUP, &ar->flag);

	timer_delete_sync(&ar->fw_recovery.hb_timer);
	cancel_work_sync(&ar->fw_recovery.recovery_work);
}

void ath6kl_recovery_suspend(struct ath6kl *ar)
{
	if (!ar->fw_recovery.enable)
		return;

	ath6kl_recovery_cleanup(ar);

	if (!ar->fw_recovery.err_reason)
		return;

	/* Process pending fw error detection */
	ar->fw_recovery.err_reason = 0;
	WARN_ON(ar->state != ATH6KL_STATE_ON);
	ar->state = ATH6KL_STATE_RECOVERY;
	ath6kl_init_hw_restart(ar);
	ar->state = ATH6KL_STATE_ON;
}

void ath6kl_recovery_resume(struct ath6kl *ar)
{
	if (!ar->fw_recovery.enable)
		return;

	clear_bit(RECOVERY_CLEANUP, &ar->flag);

	if (!ar->fw_recovery.hb_poll)
		return;

	ar->fw_recovery.hb_pending = false;
	ar->fw_recovery.seq_num = 0;
	ar->fw_recovery.hb_misscnt = 0;
	mod_timer(&ar->fw_recovery.hb_timer,
		  jiffies + msecs_to_jiffies(ar->fw_recovery.hb_poll));
}
