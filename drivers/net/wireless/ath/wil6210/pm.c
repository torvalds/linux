// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014,2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 */

#include "wil6210.h"
#include <linux/jiffies.h>
#include <linux/pm_runtime.h>

#define WIL6210_AUTOSUSPEND_DELAY_MS (1000)

static void wil_pm_wake_connected_net_queues(struct wil6210_priv *wil)
{
	int i;

	mutex_lock(&wil->vif_mutex);
	for (i = 0; i < GET_MAX_VIFS(wil); i++) {
		struct wil6210_vif *vif = wil->vifs[i];

		if (vif && test_bit(wil_vif_fwconnected, vif->status))
			wil_update_net_queues_bh(wil, vif, NULL, false);
	}
	mutex_unlock(&wil->vif_mutex);
}

static void wil_pm_stop_all_net_queues(struct wil6210_priv *wil)
{
	int i;

	mutex_lock(&wil->vif_mutex);
	for (i = 0; i < GET_MAX_VIFS(wil); i++) {
		struct wil6210_vif *vif = wil->vifs[i];

		if (vif)
			wil_update_net_queues_bh(wil, vif, NULL, true);
	}
	mutex_unlock(&wil->vif_mutex);
}

static bool
wil_can_suspend_vif(struct wil6210_priv *wil, struct wil6210_vif *vif,
		    bool is_runtime)
{
	struct wireless_dev *wdev = vif_to_wdev(vif);

	switch (wdev->iftype) {
	case NL80211_IFTYPE_MONITOR:
		wil_dbg_pm(wil, "Sniffer\n");
		return false;

	/* for STA-like interface, don't runtime suspend */
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		if (test_bit(wil_vif_fwconnecting, vif->status)) {
			wil_dbg_pm(wil, "Delay suspend when connecting\n");
			return false;
		}
		if (is_runtime) {
			wil_dbg_pm(wil, "STA-like interface\n");
			return false;
		}
		break;
	/* AP-like interface - can't suspend */
	default:
		wil_dbg_pm(wil, "AP-like interface\n");
		return false;
	}

	return true;
}

int wil_can_suspend(struct wil6210_priv *wil, bool is_runtime)
{
	int rc = 0, i;
	bool wmi_only = test_bit(WMI_FW_CAPABILITY_WMI_ONLY,
				 wil->fw_capabilities);
	bool active_ifaces;

	wil_dbg_pm(wil, "can_suspend: %s\n", is_runtime ? "runtime" : "system");

	if (wmi_only || debug_fw) {
		wil_dbg_pm(wil, "Deny any suspend - %s mode\n",
			   wmi_only ? "wmi_only" : "debug_fw");
		rc = -EBUSY;
		goto out;
	}
	if (is_runtime && !wil->platform_ops.suspend) {
		rc = -EBUSY;
		goto out;
	}

	mutex_lock(&wil->vif_mutex);
	active_ifaces = wil_has_active_ifaces(wil, true, false);
	mutex_unlock(&wil->vif_mutex);

	if (!active_ifaces) {
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
	mutex_lock(&wil->vif_mutex);
	for (i = 0; i < GET_MAX_VIFS(wil); i++) {
		struct wil6210_vif *vif = wil->vifs[i];

		if (!vif)
			continue;
		if (!wil_can_suspend_vif(wil, vif, is_runtime)) {
			rc = -EBUSY;
			mutex_unlock(&wil->vif_mutex);
			goto out;
		}
	}
	mutex_unlock(&wil->vif_mutex);

out:
	wil_dbg_pm(wil, "can_suspend: %s => %s (%d)\n",
		   is_runtime ? "runtime" : "system", rc ? "No" : "Yes", rc);

	if (rc)
		wil->suspend_stats.rejected_by_host++;

	return rc;
}

static int wil_resume_keep_radio_on(struct wil6210_priv *wil)
{
	int rc = 0;

	/* wil_status_resuming will be cleared when getting
	 * WMI_TRAFFIC_RESUME_EVENTID
	 */
	set_bit(wil_status_resuming, wil->status);
	clear_bit(wil_status_suspended, wil->status);
	wil_c(wil, RGF_USER_CLKS_CTL_0, BIT_USER_CLKS_RST_PWGD);
	wil_unmask_irq(wil);

	wil6210_bus_request(wil, wil->bus_request_kbps_pre_suspend);

	/* Send WMI resume request to the device */
	rc = wmi_resume(wil);
	if (rc) {
		wil_err(wil, "device failed to resume (%d)\n", rc);
		if (no_fw_recovery)
			goto out;
		rc = wil_down(wil);
		if (rc) {
			wil_err(wil, "wil_down failed (%d)\n", rc);
			goto out;
		}
		rc = wil_up(wil);
		if (rc) {
			wil_err(wil, "wil_up failed (%d)\n", rc);
			goto out;
		}
	}

	/* Wake all queues */
	wil_pm_wake_connected_net_queues(wil);

out:
	if (rc)
		set_bit(wil_status_suspended, wil->status);
	return rc;
}

static int wil_suspend_keep_radio_on(struct wil6210_priv *wil)
{
	int rc = 0;
	unsigned long data_comp_to;

	wil_dbg_pm(wil, "suspend keep radio on\n");

	/* Prevent handling of new tx and wmi commands */
	rc = down_write_trylock(&wil->mem_lock);
	if (!rc) {
		wil_err(wil,
			"device is busy. down_write_trylock failed, returned (0x%x)\n",
			rc);
		wil->suspend_stats.rejected_by_host++;
		return -EBUSY;
	}

	set_bit(wil_status_suspending, wil->status);
	up_write(&wil->mem_lock);

	wil_pm_stop_all_net_queues(wil);

	if (!wil_is_tx_idle(wil)) {
		wil_dbg_pm(wil, "Pending TX data, reject suspend\n");
		wil->suspend_stats.rejected_by_host++;
		goto reject_suspend;
	}

	if (!wil->txrx_ops.is_rx_idle(wil)) {
		wil_dbg_pm(wil, "Pending RX data, reject suspend\n");
		wil->suspend_stats.rejected_by_host++;
		goto reject_suspend;
	}

	if (!wil_is_wmi_idle(wil)) {
		wil_dbg_pm(wil, "Pending WMI events, reject suspend\n");
		wil->suspend_stats.rejected_by_host++;
		goto reject_suspend;
	}

	/* Send WMI suspend request to the device */
	rc = wmi_suspend(wil);
	if (rc) {
		wil_dbg_pm(wil, "wmi_suspend failed, reject suspend (%d)\n",
			   rc);
		goto reject_suspend;
	}

	/* Wait for completion of the pending RX packets */
	data_comp_to = jiffies + msecs_to_jiffies(WIL_DATA_COMPLETION_TO_MS);
	if (test_bit(wil_status_napi_en, wil->status)) {
		while (!wil->txrx_ops.is_rx_idle(wil)) {
			if (time_after(jiffies, data_comp_to)) {
				if (wil->txrx_ops.is_rx_idle(wil))
					break;
				wil_err(wil,
					"TO waiting for idle RX, suspend failed\n");
				wil->suspend_stats.r_on.failed_suspends++;
				goto resume_after_fail;
			}
			wil_dbg_ratelimited(wil, "rx vring is not empty -> NAPI\n");
			napi_synchronize(&wil->napi_rx);
			msleep(20);
		}
	}

	/* In case of pending WMI events, reject the suspend
	 * and resume the device.
	 * This can happen if the device sent the WMI events before
	 * approving the suspend.
	 */
	if (!wil_is_wmi_idle(wil)) {
		wil_err(wil, "suspend failed due to pending WMI events\n");
		wil->suspend_stats.r_on.failed_suspends++;
		goto resume_after_fail;
	}

	wil_mask_irq(wil);

	/* Disable device reset on PERST */
	wil_s(wil, RGF_USER_CLKS_CTL_0, BIT_USER_CLKS_RST_PWGD);

	if (wil->platform_ops.suspend) {
		rc = wil->platform_ops.suspend(wil->platform_handle, true);
		if (rc) {
			wil_err(wil, "platform device failed to suspend (%d)\n",
				rc);
			wil->suspend_stats.r_on.failed_suspends++;
			wil_c(wil, RGF_USER_CLKS_CTL_0, BIT_USER_CLKS_RST_PWGD);
			wil_unmask_irq(wil);
			goto resume_after_fail;
		}
	}

	/* Save the current bus request to return to the same in resume */
	wil->bus_request_kbps_pre_suspend = wil->bus_request_kbps;
	wil6210_bus_request(wil, 0);

	set_bit(wil_status_suspended, wil->status);
	clear_bit(wil_status_suspending, wil->status);

	return rc;

resume_after_fail:
	set_bit(wil_status_resuming, wil->status);
	clear_bit(wil_status_suspending, wil->status);
	rc = wmi_resume(wil);
	/* if resume succeeded, reject the suspend */
	if (!rc) {
		rc = -EBUSY;
		wil_pm_wake_connected_net_queues(wil);
	}
	return rc;

reject_suspend:
	clear_bit(wil_status_suspending, wil->status);
	wil_pm_wake_connected_net_queues(wil);
	return -EBUSY;
}

static int wil_suspend_radio_off(struct wil6210_priv *wil)
{
	int rc = 0;
	bool active_ifaces;

	wil_dbg_pm(wil, "suspend radio off\n");

	rc = down_write_trylock(&wil->mem_lock);
	if (!rc) {
		wil_err(wil,
			"device is busy. down_write_trylock failed, returned (0x%x)\n",
			rc);
		wil->suspend_stats.rejected_by_host++;
		return -EBUSY;
	}

	set_bit(wil_status_suspending, wil->status);
	up_write(&wil->mem_lock);

	/* if netif up, hardware is alive, shut it down */
	mutex_lock(&wil->vif_mutex);
	active_ifaces = wil_has_active_ifaces(wil, true, false);
	mutex_unlock(&wil->vif_mutex);

	if (active_ifaces) {
		rc = wil_down(wil);
		if (rc) {
			wil_err(wil, "wil_down : %d\n", rc);
			wil->suspend_stats.r_off.failed_suspends++;
			goto out;
		}
	}

	/* Disable PCIe IRQ to prevent sporadic IRQs when PCIe is suspending */
	wil_dbg_pm(wil, "Disabling PCIe IRQ before suspending\n");
	wil_disable_irq(wil);

	if (wil->platform_ops.suspend) {
		rc = wil->platform_ops.suspend(wil->platform_handle, false);
		if (rc) {
			wil_enable_irq(wil);
			wil->suspend_stats.r_off.failed_suspends++;
			goto out;
		}
	}

	set_bit(wil_status_suspended, wil->status);

out:
	clear_bit(wil_status_suspending, wil->status);
	wil_dbg_pm(wil, "suspend radio off: %d\n", rc);

	return rc;
}

static int wil_resume_radio_off(struct wil6210_priv *wil)
{
	int rc = 0;
	bool active_ifaces;

	wil_dbg_pm(wil, "Enabling PCIe IRQ\n");
	wil_enable_irq(wil);
	/* if any netif up, bring hardware up
	 * During open(), IFF_UP set after actual device method
	 * invocation. This prevent recursive call to wil_up()
	 * wil_status_suspended will be cleared in wil_reset
	 */
	mutex_lock(&wil->vif_mutex);
	active_ifaces = wil_has_active_ifaces(wil, true, false);
	mutex_unlock(&wil->vif_mutex);
	if (active_ifaces)
		rc = wil_up(wil);
	else
		clear_bit(wil_status_suspended, wil->status);

	return rc;
}

int wil_suspend(struct wil6210_priv *wil, bool is_runtime, bool keep_radio_on)
{
	int rc = 0;

	wil_dbg_pm(wil, "suspend: %s\n", is_runtime ? "runtime" : "system");

	if (test_bit(wil_status_suspended, wil->status)) {
		wil_dbg_pm(wil, "trying to suspend while suspended\n");
		return 0;
	}

	if (!keep_radio_on)
		rc = wil_suspend_radio_off(wil);
	else
		rc = wil_suspend_keep_radio_on(wil);

	wil_dbg_pm(wil, "suspend: %s => %d\n",
		   is_runtime ? "runtime" : "system", rc);

	return rc;
}

int wil_resume(struct wil6210_priv *wil, bool is_runtime, bool keep_radio_on)
{
	int rc = 0;

	wil_dbg_pm(wil, "resume: %s\n", is_runtime ? "runtime" : "system");

	if (wil->platform_ops.resume) {
		rc = wil->platform_ops.resume(wil->platform_handle,
					      keep_radio_on);
		if (rc) {
			wil_err(wil, "platform_ops.resume : %d\n", rc);
			goto out;
		}
	}

	if (keep_radio_on)
		rc = wil_resume_keep_radio_on(wil);
	else
		rc = wil_resume_radio_off(wil);

out:
	wil_dbg_pm(wil, "resume: %s => %d\n", is_runtime ? "runtime" : "system",
		   rc);
	return rc;
}

void wil_pm_runtime_allow(struct wil6210_priv *wil)
{
	struct device *dev = wil_to_dev(wil);

	pm_runtime_put_noidle(dev);
	pm_runtime_set_autosuspend_delay(dev, WIL6210_AUTOSUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_allow(dev);
}

void wil_pm_runtime_forbid(struct wil6210_priv *wil)
{
	struct device *dev = wil_to_dev(wil);

	pm_runtime_forbid(dev);
	pm_runtime_get_noresume(dev);
}

int wil_pm_runtime_get(struct wil6210_priv *wil)
{
	int rc;
	struct device *dev = wil_to_dev(wil);

	rc = pm_runtime_resume_and_get(dev);
	if (rc < 0) {
		wil_err(wil, "pm_runtime_resume_and_get() failed, rc = %d\n", rc);
		return rc;
	}

	return 0;
}

void wil_pm_runtime_put(struct wil6210_priv *wil)
{
	struct device *dev = wil_to_dev(wil);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
}
