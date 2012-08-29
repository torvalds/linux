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

	ath6kl_init_hw_restart(ar);
}

void ath6kl_recovery_err_notify(struct ath6kl *ar, enum ath6kl_fw_err reason)
{
	ath6kl_dbg(ATH6KL_DBG_RECOVERY, "Fw error detected, reason:%d\n",
		   reason);

	set_bit(reason, &ar->fw_recovery.err_reason);

	if (ar->fw_recovery.enable && ar->state != ATH6KL_STATE_RECOVERY)
		queue_work(ar->ath6kl_wq, &ar->fw_recovery.recovery_work);
}

void ath6kl_recovery_init(struct ath6kl *ar)
{
	struct ath6kl_fw_recovery *recovery = &ar->fw_recovery;

	recovery->enable = true;
	INIT_WORK(&recovery->recovery_work, ath6kl_recovery_work);
}

void ath6kl_recovery_cleanup(struct ath6kl *ar)
{
	ar->fw_recovery.enable = false;

	cancel_work_sync(&ar->fw_recovery.recovery_work);
}

void ath6kl_recovery_suspend(struct ath6kl *ar)
{
	ath6kl_recovery_cleanup(ar);

	/* Process pending fw error detection */
	if (ar->fw_recovery.err_reason)
		ath6kl_init_hw_restart(ar);
}
