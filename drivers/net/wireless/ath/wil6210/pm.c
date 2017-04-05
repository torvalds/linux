/*
 * Copyright (c) 2014,2017 Qualcomm Atheros, Inc.
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

#include "wil6210.h"

int wil_can_suspend(struct wil6210_priv *wil, bool is_runtime)
{
	int rc = 0;
	struct wireless_dev *wdev = wil->wdev;

	wil_dbg_pm(wil, "can_suspend: %s\n", is_runtime ? "runtime" : "system");

	if (!netif_running(wil_to_ndev(wil))) {
		/* can always sleep when down */
		wil_dbg_pm(wil, "Interface is down\n");
		goto out;
	}
	if (test_bit(wil_status_resetting, wil->status)) {
		wil_dbg_pm(wil, "Delay suspend when resetting\n");
		rc = -EBUSY;
		goto out;
	}
	if (wil->recovery_state != fw_recovery_idle) {
		wil_dbg_pm(wil, "Delay suspend during recovery\n");
		rc = -EBUSY;
		goto out;
	}

	/* interface is running */
	switch (wdev->iftype) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		if (test_bit(wil_status_fwconnecting, wil->status)) {
			wil_dbg_pm(wil, "Delay suspend when connecting\n");
			rc = -EBUSY;
			goto out;
		}
		break;
	/* AP-like interface - can't suspend */
	default:
		wil_dbg_pm(wil, "AP-like interface\n");
		rc = -EBUSY;
		break;
	}

out:
	wil_dbg_pm(wil, "can_suspend: %s => %s (%d)\n",
		   is_runtime ? "runtime" : "system", rc ? "No" : "Yes", rc);

	return rc;
}

int wil_suspend(struct wil6210_priv *wil, bool is_runtime)
{
	int rc = 0;
	struct net_device *ndev = wil_to_ndev(wil);

	wil_dbg_pm(wil, "suspend: %s\n", is_runtime ? "runtime" : "system");

	/* if netif up, hardware is alive, shut it down */
	if (ndev->flags & IFF_UP) {
		rc = wil_down(wil);
		if (rc) {
			wil_err(wil, "wil_down : %d\n", rc);
			goto out;
		}
	}

	/* Disable PCIe IRQ to prevent sporadic IRQs when PCIe is suspending */
	wil_dbg_pm(wil, "Disabling PCIe IRQ before suspending\n");
	wil_disable_irq(wil);

	if (wil->platform_ops.suspend) {
		rc = wil->platform_ops.suspend(wil->platform_handle);
		if (rc)
			wil_enable_irq(wil);
	}

out:
	wil_dbg_pm(wil, "suspend: %s => %d\n",
		   is_runtime ? "runtime" : "system", rc);

	return rc;
}

int wil_resume(struct wil6210_priv *wil, bool is_runtime)
{
	int rc = 0;
	struct net_device *ndev = wil_to_ndev(wil);

	wil_dbg_pm(wil, "resume: %s\n", is_runtime ? "runtime" : "system");

	if (wil->platform_ops.resume) {
		rc = wil->platform_ops.resume(wil->platform_handle);
		if (rc) {
			wil_err(wil, "platform_ops.resume : %d\n", rc);
			goto out;
		}
	}

	wil_dbg_pm(wil, "Enabling PCIe IRQ\n");
	wil_enable_irq(wil);

	/* if netif up, bring hardware up
	 * During open(), IFF_UP set after actual device method
	 * invocation. This prevent recursive call to wil_up()
	 */
	if (ndev->flags & IFF_UP)
		rc = wil_up(wil);

out:
	wil_dbg_pm(wil, "resume: %s => %d\n",
		   is_runtime ? "runtime" : "system", rc);
	return rc;
}
