// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of wlcore
 *
 * Copyright (C) 2008-2010 Nokia Corporation
 * Copyright (C) 2011-2013 Texas Instruments Inc.
 */

#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>

#include "wlcore.h"
#include "debug.h"
#include "wl12xx_80211.h"
#include "io.h"
#include "tx.h"
#include "ps.h"
#include "init.h"
#include "debugfs.h"
#include "testmode.h"
#include "vendor_cmd.h"
#include "scan.h"
#include "hw_ops.h"
#include "sysfs.h"

#define WL1271_BOOT_RETRIES 3
#define WL1271_SUSPEND_SLEEP 100
#define WL1271_WAKEUP_TIMEOUT 500

static char *fwlog_param;
static int fwlog_mem_blocks = -1;
static int bug_on_recovery = -1;
static int no_recovery     = -1;

static void __wl1271_op_remove_interface(struct wl1271 *wl,
					 struct ieee80211_vif *vif,
					 bool reset_tx_queues);
static void wlcore_op_stop_locked(struct wl1271 *wl);
static void wl1271_free_ap_keys(struct wl1271 *wl, struct wl12xx_vif *wlvif);

static int wl12xx_set_authorized(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	int ret;

	if (WARN_ON(wlvif->bss_type != BSS_TYPE_STA_BSS))
		return -EINVAL;

	if (!test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
		return 0;

	if (test_and_set_bit(WLVIF_FLAG_STA_STATE_SENT, &wlvif->flags))
		return 0;

	ret = wl12xx_cmd_set_peer_state(wl, wlvif, wlvif->sta.hlid);
	if (ret < 0)
		return ret;

	wl1271_info("Association completed.");
	return 0;
}

static void wl1271_reg_notify(struct wiphy *wiphy,
			      struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct wl1271 *wl = hw->priv;

	/* copy the current dfs region */
	if (request)
		wl->dfs_region = request->dfs_region;

	wlcore_regdomain_config(wl);
}

static int wl1271_set_rx_streaming(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				   bool enable)
{
	int ret = 0;

	/* we should hold wl->mutex */
	ret = wl1271_acx_ps_rx_streaming(wl, wlvif, enable);
	if (ret < 0)
		goto out;

	if (enable)
		set_bit(WLVIF_FLAG_RX_STREAMING_STARTED, &wlvif->flags);
	else
		clear_bit(WLVIF_FLAG_RX_STREAMING_STARTED, &wlvif->flags);
out:
	return ret;
}

/*
 * this function is being called when the rx_streaming interval
 * has beed changed or rx_streaming should be disabled
 */
int wl1271_recalc_rx_streaming(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	int ret = 0;
	int period = wl->conf.rx_streaming.interval;

	/* don't reconfigure if rx_streaming is disabled */
	if (!test_bit(WLVIF_FLAG_RX_STREAMING_STARTED, &wlvif->flags))
		goto out;

	/* reconfigure/disable according to new streaming_period */
	if (period &&
	    test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags) &&
	    (wl->conf.rx_streaming.always ||
	     test_bit(WL1271_FLAG_SOFT_GEMINI, &wl->flags)))
		ret = wl1271_set_rx_streaming(wl, wlvif, true);
	else {
		ret = wl1271_set_rx_streaming(wl, wlvif, false);
		/* don't cancel_work_sync since we might deadlock */
		del_timer_sync(&wlvif->rx_streaming_timer);
	}
out:
	return ret;
}

static void wl1271_rx_streaming_enable_work(struct work_struct *work)
{
	int ret;
	struct wl12xx_vif *wlvif = container_of(work, struct wl12xx_vif,
						rx_streaming_enable_work);
	struct wl1271 *wl = wlvif->wl;

	mutex_lock(&wl->mutex);

	if (test_bit(WLVIF_FLAG_RX_STREAMING_STARTED, &wlvif->flags) ||
	    !test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags) ||
	    (!wl->conf.rx_streaming.always &&
	     !test_bit(WL1271_FLAG_SOFT_GEMINI, &wl->flags)))
		goto out;

	if (!wl->conf.rx_streaming.interval)
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	ret = wl1271_set_rx_streaming(wl, wlvif, true);
	if (ret < 0)
		goto out_sleep;

	/* stop it after some time of inactivity */
	mod_timer(&wlvif->rx_streaming_timer,
		  jiffies + msecs_to_jiffies(wl->conf.rx_streaming.duration));

out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);
}

static void wl1271_rx_streaming_disable_work(struct work_struct *work)
{
	int ret;
	struct wl12xx_vif *wlvif = container_of(work, struct wl12xx_vif,
						rx_streaming_disable_work);
	struct wl1271 *wl = wlvif->wl;

	mutex_lock(&wl->mutex);

	if (!test_bit(WLVIF_FLAG_RX_STREAMING_STARTED, &wlvif->flags))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	ret = wl1271_set_rx_streaming(wl, wlvif, false);
	if (ret)
		goto out_sleep;

out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);
}

static void wl1271_rx_streaming_timer(struct timer_list *t)
{
	struct wl12xx_vif *wlvif = from_timer(wlvif, t, rx_streaming_timer);
	struct wl1271 *wl = wlvif->wl;
	ieee80211_queue_work(wl->hw, &wlvif->rx_streaming_disable_work);
}

/* wl->mutex must be taken */
void wl12xx_rearm_tx_watchdog_locked(struct wl1271 *wl)
{
	/* if the watchdog is not armed, don't do anything */
	if (wl->tx_allocated_blocks == 0)
		return;

	cancel_delayed_work(&wl->tx_watchdog_work);
	ieee80211_queue_delayed_work(wl->hw, &wl->tx_watchdog_work,
		msecs_to_jiffies(wl->conf.tx.tx_watchdog_timeout));
}

static void wlcore_rc_update_work(struct work_struct *work)
{
	int ret;
	struct wl12xx_vif *wlvif = container_of(work, struct wl12xx_vif,
						rc_update_work);
	struct wl1271 *wl = wlvif->wl;
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	if (ieee80211_vif_is_mesh(vif)) {
		ret = wl1271_acx_set_ht_capabilities(wl, &wlvif->rc_ht_cap,
						     true, wlvif->sta.hlid);
		if (ret < 0)
			goto out_sleep;
	} else {
		wlcore_hw_sta_rc_update(wl, wlvif);
	}

out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);
}

static void wl12xx_tx_watchdog_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wl1271 *wl;

	dwork = to_delayed_work(work);
	wl = container_of(dwork, struct wl1271, tx_watchdog_work);

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	/* Tx went out in the meantime - everything is ok */
	if (unlikely(wl->tx_allocated_blocks == 0))
		goto out;

	/*
	 * if a ROC is in progress, we might not have any Tx for a long
	 * time (e.g. pending Tx on the non-ROC channels)
	 */
	if (find_first_bit(wl->roc_map, WL12XX_MAX_ROLES) < WL12XX_MAX_ROLES) {
		wl1271_debug(DEBUG_TX, "No Tx (in FW) for %d ms due to ROC",
			     wl->conf.tx.tx_watchdog_timeout);
		wl12xx_rearm_tx_watchdog_locked(wl);
		goto out;
	}

	/*
	 * if a scan is in progress, we might not have any Tx for a long
	 * time
	 */
	if (wl->scan.state != WL1271_SCAN_STATE_IDLE) {
		wl1271_debug(DEBUG_TX, "No Tx (in FW) for %d ms due to scan",
			     wl->conf.tx.tx_watchdog_timeout);
		wl12xx_rearm_tx_watchdog_locked(wl);
		goto out;
	}

	/*
	* AP might cache a frame for a long time for a sleeping station,
	* so rearm the timer if there's an AP interface with stations. If
	* Tx is genuinely stuck we will most hopefully discover it when all
	* stations are removed due to inactivity.
	*/
	if (wl->active_sta_count) {
		wl1271_debug(DEBUG_TX, "No Tx (in FW) for %d ms. AP has "
			     " %d stations",
			      wl->conf.tx.tx_watchdog_timeout,
			      wl->active_sta_count);
		wl12xx_rearm_tx_watchdog_locked(wl);
		goto out;
	}

	wl1271_error("Tx stuck (in FW) for %d ms. Starting recovery",
		     wl->conf.tx.tx_watchdog_timeout);
	wl12xx_queue_recovery_work(wl);

out:
	mutex_unlock(&wl->mutex);
}

static void wlcore_adjust_conf(struct wl1271 *wl)
{

	if (fwlog_param) {
		if (!strcmp(fwlog_param, "continuous")) {
			wl->conf.fwlog.mode = WL12XX_FWLOG_CONTINUOUS;
			wl->conf.fwlog.output = WL12XX_FWLOG_OUTPUT_HOST;
		} else if (!strcmp(fwlog_param, "dbgpins")) {
			wl->conf.fwlog.mode = WL12XX_FWLOG_CONTINUOUS;
			wl->conf.fwlog.output = WL12XX_FWLOG_OUTPUT_DBG_PINS;
		} else if (!strcmp(fwlog_param, "disable")) {
			wl->conf.fwlog.mem_blocks = 0;
			wl->conf.fwlog.output = WL12XX_FWLOG_OUTPUT_NONE;
		} else {
			wl1271_error("Unknown fwlog parameter %s", fwlog_param);
		}
	}

	if (bug_on_recovery != -1)
		wl->conf.recovery.bug_on_recovery = (u8) bug_on_recovery;

	if (no_recovery != -1)
		wl->conf.recovery.no_recovery = (u8) no_recovery;
}

static void wl12xx_irq_ps_regulate_link(struct wl1271 *wl,
					struct wl12xx_vif *wlvif,
					u8 hlid, u8 tx_pkts)
{
	bool fw_ps;

	fw_ps = test_bit(hlid, &wl->ap_fw_ps_map);

	/*
	 * Wake up from high level PS if the STA is asleep with too little
	 * packets in FW or if the STA is awake.
	 */
	if (!fw_ps || tx_pkts < WL1271_PS_STA_MAX_PACKETS)
		wl12xx_ps_link_end(wl, wlvif, hlid);

	/*
	 * Start high-level PS if the STA is asleep with enough blocks in FW.
	 * Make an exception if this is the only connected link. In this
	 * case FW-memory congestion is less of a problem.
	 * Note that a single connected STA means 2*ap_count + 1 active links,
	 * since we must account for the global and broadcast AP links
	 * for each AP. The "fw_ps" check assures us the other link is a STA
	 * connected to the AP. Otherwise the FW would not set the PSM bit.
	 */
	else if (wl->active_link_count > (wl->ap_count*2 + 1) && fw_ps &&
		 tx_pkts >= WL1271_PS_STA_MAX_PACKETS)
		wl12xx_ps_link_start(wl, wlvif, hlid, true);
}

static void wl12xx_irq_update_links_status(struct wl1271 *wl,
					   struct wl12xx_vif *wlvif,
					   struct wl_fw_status *status)
{
	unsigned long cur_fw_ps_map;
	u8 hlid;

	cur_fw_ps_map = status->link_ps_bitmap;
	if (wl->ap_fw_ps_map != cur_fw_ps_map) {
		wl1271_debug(DEBUG_PSM,
			     "link ps prev 0x%lx cur 0x%lx changed 0x%lx",
			     wl->ap_fw_ps_map, cur_fw_ps_map,
			     wl->ap_fw_ps_map ^ cur_fw_ps_map);

		wl->ap_fw_ps_map = cur_fw_ps_map;
	}

	for_each_set_bit(hlid, wlvif->ap.sta_hlid_map, wl->num_links)
		wl12xx_irq_ps_regulate_link(wl, wlvif, hlid,
					    wl->links[hlid].allocated_pkts);
}

static int wlcore_fw_status(struct wl1271 *wl, struct wl_fw_status *status)
{
	struct wl12xx_vif *wlvif;
	u32 old_tx_blk_count = wl->tx_blocks_available;
	int avail, freed_blocks;
	int i;
	int ret;
	struct wl1271_link *lnk;

	ret = wlcore_raw_read_data(wl, REG_RAW_FW_STATUS_ADDR,
				   wl->raw_fw_status,
				   wl->fw_status_len, false);
	if (ret < 0)
		return ret;

	wlcore_hw_convert_fw_status(wl, wl->raw_fw_status, wl->fw_status);

	wl1271_debug(DEBUG_IRQ, "intr: 0x%x (fw_rx_counter = %d, "
		     "drv_rx_counter = %d, tx_results_counter = %d)",
		     status->intr,
		     status->fw_rx_counter,
		     status->drv_rx_counter,
		     status->tx_results_counter);

	for (i = 0; i < NUM_TX_QUEUES; i++) {
		/* prevent wrap-around in freed-packets counter */
		wl->tx_allocated_pkts[i] -=
				(status->counters.tx_released_pkts[i] -
				wl->tx_pkts_freed[i]) & 0xff;

		wl->tx_pkts_freed[i] = status->counters.tx_released_pkts[i];
	}


	for_each_set_bit(i, wl->links_map, wl->num_links) {
		u8 diff;
		lnk = &wl->links[i];

		/* prevent wrap-around in freed-packets counter */
		diff = (status->counters.tx_lnk_free_pkts[i] -
		       lnk->prev_freed_pkts) & 0xff;

		if (diff == 0)
			continue;

		lnk->allocated_pkts -= diff;
		lnk->prev_freed_pkts = status->counters.tx_lnk_free_pkts[i];

		/* accumulate the prev_freed_pkts counter */
		lnk->total_freed_pkts += diff;
	}

	/* prevent wrap-around in total blocks counter */
	if (likely(wl->tx_blocks_freed <= status->total_released_blks))
		freed_blocks = status->total_released_blks -
			       wl->tx_blocks_freed;
	else
		freed_blocks = 0x100000000LL - wl->tx_blocks_freed +
			       status->total_released_blks;

	wl->tx_blocks_freed = status->total_released_blks;

	wl->tx_allocated_blocks -= freed_blocks;

	/*
	 * If the FW freed some blocks:
	 * If we still have allocated blocks - re-arm the timer, Tx is
	 * not stuck. Otherwise, cancel the timer (no Tx currently).
	 */
	if (freed_blocks) {
		if (wl->tx_allocated_blocks)
			wl12xx_rearm_tx_watchdog_locked(wl);
		else
			cancel_delayed_work(&wl->tx_watchdog_work);
	}

	avail = status->tx_total - wl->tx_allocated_blocks;

	/*
	 * The FW might change the total number of TX memblocks before
	 * we get a notification about blocks being released. Thus, the
	 * available blocks calculation might yield a temporary result
	 * which is lower than the actual available blocks. Keeping in
	 * mind that only blocks that were allocated can be moved from
	 * TX to RX, tx_blocks_available should never decrease here.
	 */
	wl->tx_blocks_available = max((int)wl->tx_blocks_available,
				      avail);

	/* if more blocks are available now, tx work can be scheduled */
	if (wl->tx_blocks_available > old_tx_blk_count)
		clear_bit(WL1271_FLAG_FW_TX_BUSY, &wl->flags);

	/* for AP update num of allocated TX blocks per link and ps status */
	wl12xx_for_each_wlvif_ap(wl, wlvif) {
		wl12xx_irq_update_links_status(wl, wlvif, status);
	}

	/* update the host-chipset time offset */
	wl->time_offset = (ktime_get_boottime_ns() >> 10) -
		(s64)(status->fw_localtime);

	wl->fw_fast_lnk_map = status->link_fast_bitmap;

	return 0;
}

static void wl1271_flush_deferred_work(struct wl1271 *wl)
{
	struct sk_buff *skb;

	/* Pass all received frames to the network stack */
	while ((skb = skb_dequeue(&wl->deferred_rx_queue)))
		ieee80211_rx_ni(wl->hw, skb);

	/* Return sent skbs to the network stack */
	while ((skb = skb_dequeue(&wl->deferred_tx_queue)))
		ieee80211_tx_status_ni(wl->hw, skb);
}

static void wl1271_netstack_work(struct work_struct *work)
{
	struct wl1271 *wl =
		container_of(work, struct wl1271, netstack_work);

	do {
		wl1271_flush_deferred_work(wl);
	} while (skb_queue_len(&wl->deferred_rx_queue));
}

#define WL1271_IRQ_MAX_LOOPS 256

static int wlcore_irq_locked(struct wl1271 *wl)
{
	int ret = 0;
	u32 intr;
	int loopcount = WL1271_IRQ_MAX_LOOPS;
	bool done = false;
	unsigned int defer_count;
	unsigned long flags;

	/*
	 * In case edge triggered interrupt must be used, we cannot iterate
	 * more than once without introducing race conditions with the hardirq.
	 */
	if (wl->irq_flags & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING))
		loopcount = 1;

	wl1271_debug(DEBUG_IRQ, "IRQ work");

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	while (!done && loopcount--) {
		smp_mb__after_atomic();

		ret = wlcore_fw_status(wl, wl->fw_status);
		if (ret < 0)
			goto out;

		wlcore_hw_tx_immediate_compl(wl);

		intr = wl->fw_status->intr;
		intr &= WLCORE_ALL_INTR_MASK;
		if (!intr) {
			done = true;
			continue;
		}

		if (unlikely(intr & WL1271_ACX_INTR_WATCHDOG)) {
			wl1271_error("HW watchdog interrupt received! starting recovery.");
			wl->watchdog_recovery = true;
			ret = -EIO;

			/* restarting the chip. ignore any other interrupt. */
			goto out;
		}

		if (unlikely(intr & WL1271_ACX_SW_INTR_WATCHDOG)) {
			wl1271_error("SW watchdog interrupt received! "
				     "starting recovery.");
			wl->watchdog_recovery = true;
			ret = -EIO;

			/* restarting the chip. ignore any other interrupt. */
			goto out;
		}

		if (likely(intr & WL1271_ACX_INTR_DATA)) {
			wl1271_debug(DEBUG_IRQ, "WL1271_ACX_INTR_DATA");

			ret = wlcore_rx(wl, wl->fw_status);
			if (ret < 0)
				goto out;

			/* Check if any tx blocks were freed */
			spin_lock_irqsave(&wl->wl_lock, flags);
			if (!test_bit(WL1271_FLAG_FW_TX_BUSY, &wl->flags) &&
			    wl1271_tx_total_queue_count(wl) > 0) {
				spin_unlock_irqrestore(&wl->wl_lock, flags);
				/*
				 * In order to avoid starvation of the TX path,
				 * call the work function directly.
				 */
				ret = wlcore_tx_work_locked(wl);
				if (ret < 0)
					goto out;
			} else {
				spin_unlock_irqrestore(&wl->wl_lock, flags);
			}

			/* check for tx results */
			ret = wlcore_hw_tx_delayed_compl(wl);
			if (ret < 0)
				goto out;

			/* Make sure the deferred queues don't get too long */
			defer_count = skb_queue_len(&wl->deferred_tx_queue) +
				      skb_queue_len(&wl->deferred_rx_queue);
			if (defer_count > WL1271_DEFERRED_QUEUE_LIMIT)
				wl1271_flush_deferred_work(wl);
		}

		if (intr & WL1271_ACX_INTR_EVENT_A) {
			wl1271_debug(DEBUG_IRQ, "WL1271_ACX_INTR_EVENT_A");
			ret = wl1271_event_handle(wl, 0);
			if (ret < 0)
				goto out;
		}

		if (intr & WL1271_ACX_INTR_EVENT_B) {
			wl1271_debug(DEBUG_IRQ, "WL1271_ACX_INTR_EVENT_B");
			ret = wl1271_event_handle(wl, 1);
			if (ret < 0)
				goto out;
		}

		if (intr & WL1271_ACX_INTR_INIT_COMPLETE)
			wl1271_debug(DEBUG_IRQ,
				     "WL1271_ACX_INTR_INIT_COMPLETE");

		if (intr & WL1271_ACX_INTR_HW_AVAILABLE)
			wl1271_debug(DEBUG_IRQ, "WL1271_ACX_INTR_HW_AVAILABLE");
	}

	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

out:
	return ret;
}

static irqreturn_t wlcore_irq(int irq, void *cookie)
{
	int ret;
	unsigned long flags;
	struct wl1271 *wl = cookie;

	/* complete the ELP completion */
	spin_lock_irqsave(&wl->wl_lock, flags);
	set_bit(WL1271_FLAG_IRQ_RUNNING, &wl->flags);
	if (wl->elp_compl) {
		complete(wl->elp_compl);
		wl->elp_compl = NULL;
	}

	if (test_bit(WL1271_FLAG_SUSPENDED, &wl->flags)) {
		/* don't enqueue a work right now. mark it as pending */
		set_bit(WL1271_FLAG_PENDING_WORK, &wl->flags);
		wl1271_debug(DEBUG_IRQ, "should not enqueue work");
		disable_irq_nosync(wl->irq);
		pm_wakeup_event(wl->dev, 0);
		spin_unlock_irqrestore(&wl->wl_lock, flags);
		goto out_handled;
	}
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	/* TX might be handled here, avoid redundant work */
	set_bit(WL1271_FLAG_TX_PENDING, &wl->flags);
	cancel_work_sync(&wl->tx_work);

	mutex_lock(&wl->mutex);

	ret = wlcore_irq_locked(wl);
	if (ret)
		wl12xx_queue_recovery_work(wl);

	spin_lock_irqsave(&wl->wl_lock, flags);
	/* In case TX was not handled here, queue TX work */
	clear_bit(WL1271_FLAG_TX_PENDING, &wl->flags);
	if (!test_bit(WL1271_FLAG_FW_TX_BUSY, &wl->flags) &&
	    wl1271_tx_total_queue_count(wl) > 0)
		ieee80211_queue_work(wl->hw, &wl->tx_work);
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	mutex_unlock(&wl->mutex);

out_handled:
	spin_lock_irqsave(&wl->wl_lock, flags);
	clear_bit(WL1271_FLAG_IRQ_RUNNING, &wl->flags);
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	return IRQ_HANDLED;
}

struct vif_counter_data {
	u8 counter;

	struct ieee80211_vif *cur_vif;
	bool cur_vif_running;
};

static void wl12xx_vif_count_iter(void *data, u8 *mac,
				  struct ieee80211_vif *vif)
{
	struct vif_counter_data *counter = data;

	counter->counter++;
	if (counter->cur_vif == vif)
		counter->cur_vif_running = true;
}

/* caller must not hold wl->mutex, as it might deadlock */
static void wl12xx_get_vif_count(struct ieee80211_hw *hw,
			       struct ieee80211_vif *cur_vif,
			       struct vif_counter_data *data)
{
	memset(data, 0, sizeof(*data));
	data->cur_vif = cur_vif;

	ieee80211_iterate_active_interfaces(hw, IEEE80211_IFACE_ITER_RESUME_ALL,
					    wl12xx_vif_count_iter, data);
}

static int wl12xx_fetch_firmware(struct wl1271 *wl, bool plt)
{
	const struct firmware *fw;
	const char *fw_name;
	enum wl12xx_fw_type fw_type;
	int ret;

	if (plt) {
		fw_type = WL12XX_FW_TYPE_PLT;
		fw_name = wl->plt_fw_name;
	} else {
		/*
		 * we can't call wl12xx_get_vif_count() here because
		 * wl->mutex is taken, so use the cached last_vif_count value
		 */
		if (wl->last_vif_count > 1 && wl->mr_fw_name) {
			fw_type = WL12XX_FW_TYPE_MULTI;
			fw_name = wl->mr_fw_name;
		} else {
			fw_type = WL12XX_FW_TYPE_NORMAL;
			fw_name = wl->sr_fw_name;
		}
	}

	if (wl->fw_type == fw_type)
		return 0;

	wl1271_debug(DEBUG_BOOT, "booting firmware %s", fw_name);

	ret = request_firmware(&fw, fw_name, wl->dev);

	if (ret < 0) {
		wl1271_error("could not get firmware %s: %d", fw_name, ret);
		return ret;
	}

	if (fw->size % 4) {
		wl1271_error("firmware size is not multiple of 32 bits: %zu",
			     fw->size);
		ret = -EILSEQ;
		goto out;
	}

	vfree(wl->fw);
	wl->fw_type = WL12XX_FW_TYPE_NONE;
	wl->fw_len = fw->size;
	wl->fw = vmalloc(wl->fw_len);

	if (!wl->fw) {
		wl1271_error("could not allocate memory for the firmware");
		ret = -ENOMEM;
		goto out;
	}

	memcpy(wl->fw, fw->data, wl->fw_len);
	ret = 0;
	wl->fw_type = fw_type;
out:
	release_firmware(fw);

	return ret;
}

void wl12xx_queue_recovery_work(struct wl1271 *wl)
{
	/* Avoid a recursive recovery */
	if (wl->state == WLCORE_STATE_ON) {
		WARN_ON(!test_bit(WL1271_FLAG_INTENDED_FW_RECOVERY,
				  &wl->flags));

		wl->state = WLCORE_STATE_RESTARTING;
		set_bit(WL1271_FLAG_RECOVERY_IN_PROGRESS, &wl->flags);
		ieee80211_queue_work(wl->hw, &wl->recovery_work);
	}
}

size_t wl12xx_copy_fwlog(struct wl1271 *wl, u8 *memblock, size_t maxlen)
{
	size_t len;

	/* Make sure we have enough room */
	len = min_t(size_t, maxlen, PAGE_SIZE - wl->fwlog_size);

	/* Fill the FW log file, consumed by the sysfs fwlog entry */
	memcpy(wl->fwlog + wl->fwlog_size, memblock, len);
	wl->fwlog_size += len;

	return len;
}

static void wl12xx_read_fwlog_panic(struct wl1271 *wl)
{
	u32 end_of_log = 0;
	int error;

	if (wl->quirks & WLCORE_QUIRK_FWLOG_NOT_IMPLEMENTED)
		return;

	wl1271_info("Reading FW panic log");

	/*
	 * Make sure the chip is awake and the logger isn't active.
	 * Do not send a stop fwlog command if the fw is hanged or if
	 * dbgpins are used (due to some fw bug).
	 */
	error = pm_runtime_get_sync(wl->dev);
	if (error < 0) {
		pm_runtime_put_noidle(wl->dev);
		return;
	}
	if (!wl->watchdog_recovery &&
	    wl->conf.fwlog.output != WL12XX_FWLOG_OUTPUT_DBG_PINS)
		wl12xx_cmd_stop_fwlog(wl);

	/* Traverse the memory blocks linked list */
	do {
		end_of_log = wlcore_event_fw_logger(wl);
		if (end_of_log == 0) {
			msleep(100);
			end_of_log = wlcore_event_fw_logger(wl);
		}
	} while (end_of_log != 0);
}

static void wlcore_save_freed_pkts(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				   u8 hlid, struct ieee80211_sta *sta)
{
	struct wl1271_station *wl_sta;
	u32 sqn_recovery_padding = WL1271_TX_SQN_POST_RECOVERY_PADDING;

	wl_sta = (void *)sta->drv_priv;
	wl_sta->total_freed_pkts = wl->links[hlid].total_freed_pkts;

	/*
	 * increment the initial seq number on recovery to account for
	 * transmitted packets that we haven't yet got in the FW status
	 */
	if (wlvif->encryption_type == KEY_GEM)
		sqn_recovery_padding = WL1271_TX_SQN_POST_RECOVERY_PADDING_GEM;

	if (test_bit(WL1271_FLAG_RECOVERY_IN_PROGRESS, &wl->flags))
		wl_sta->total_freed_pkts += sqn_recovery_padding;
}

static void wlcore_save_freed_pkts_addr(struct wl1271 *wl,
					struct wl12xx_vif *wlvif,
					u8 hlid, const u8 *addr)
{
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);

	if (WARN_ON(hlid == WL12XX_INVALID_LINK_ID ||
		    is_zero_ether_addr(addr)))
		return;

	rcu_read_lock();
	sta = ieee80211_find_sta(vif, addr);
	if (sta)
		wlcore_save_freed_pkts(wl, wlvif, hlid, sta);
	rcu_read_unlock();
}

static void wlcore_print_recovery(struct wl1271 *wl)
{
	u32 pc = 0;
	u32 hint_sts = 0;
	int ret;

	wl1271_info("Hardware recovery in progress. FW ver: %s",
		    wl->chip.fw_ver_str);

	/* change partitions momentarily so we can read the FW pc */
	ret = wlcore_set_partition(wl, &wl->ptable[PART_BOOT]);
	if (ret < 0)
		return;

	ret = wlcore_read_reg(wl, REG_PC_ON_RECOVERY, &pc);
	if (ret < 0)
		return;

	ret = wlcore_read_reg(wl, REG_INTERRUPT_NO_CLEAR, &hint_sts);
	if (ret < 0)
		return;

	wl1271_info("pc: 0x%x, hint_sts: 0x%08x count: %d",
				pc, hint_sts, ++wl->recovery_count);

	wlcore_set_partition(wl, &wl->ptable[PART_WORK]);
}


static void wl1271_recovery_work(struct work_struct *work)
{
	struct wl1271 *wl =
		container_of(work, struct wl1271, recovery_work);
	struct wl12xx_vif *wlvif;
	struct ieee80211_vif *vif;
	int error;

	mutex_lock(&wl->mutex);

	if (wl->state == WLCORE_STATE_OFF || wl->plt)
		goto out_unlock;

	error = pm_runtime_get_sync(wl->dev);
	if (error < 0) {
		wl1271_warning("Enable for recovery failed");
		pm_runtime_put_noidle(wl->dev);
	}
	wlcore_disable_interrupts_nosync(wl);

	if (!test_bit(WL1271_FLAG_INTENDED_FW_RECOVERY, &wl->flags)) {
		if (wl->conf.fwlog.output == WL12XX_FWLOG_OUTPUT_HOST)
			wl12xx_read_fwlog_panic(wl);
		wlcore_print_recovery(wl);
	}

	BUG_ON(wl->conf.recovery.bug_on_recovery &&
	       !test_bit(WL1271_FLAG_INTENDED_FW_RECOVERY, &wl->flags));

	clear_bit(WL1271_FLAG_INTENDED_FW_RECOVERY, &wl->flags);

	if (wl->conf.recovery.no_recovery) {
		wl1271_info("No recovery (chosen on module load). Fw will remain stuck.");
		goto out_unlock;
	}

	/* Prevent spurious TX during FW restart */
	wlcore_stop_queues(wl, WLCORE_QUEUE_STOP_REASON_FW_RESTART);

	/* reboot the chipset */
	while (!list_empty(&wl->wlvif_list)) {
		wlvif = list_first_entry(&wl->wlvif_list,
				       struct wl12xx_vif, list);
		vif = wl12xx_wlvif_to_vif(wlvif);

		if (wlvif->bss_type == BSS_TYPE_STA_BSS &&
		    test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags)) {
			wlcore_save_freed_pkts_addr(wl, wlvif, wlvif->sta.hlid,
						    vif->bss_conf.bssid);
		}

		__wl1271_op_remove_interface(wl, vif, false);
	}

	wlcore_op_stop_locked(wl);
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

	ieee80211_restart_hw(wl->hw);

	/*
	 * Its safe to enable TX now - the queues are stopped after a request
	 * to restart the HW.
	 */
	wlcore_wake_queues(wl, WLCORE_QUEUE_STOP_REASON_FW_RESTART);

out_unlock:
	wl->watchdog_recovery = false;
	clear_bit(WL1271_FLAG_RECOVERY_IN_PROGRESS, &wl->flags);
	mutex_unlock(&wl->mutex);
}

static int wlcore_fw_wakeup(struct wl1271 *wl)
{
	return wlcore_raw_write32(wl, HW_ACCESS_ELP_CTRL_REG, ELPCTRL_WAKE_UP);
}

static int wl1271_setup(struct wl1271 *wl)
{
	wl->raw_fw_status = kzalloc(wl->fw_status_len, GFP_KERNEL);
	if (!wl->raw_fw_status)
		goto err;

	wl->fw_status = kzalloc(sizeof(*wl->fw_status), GFP_KERNEL);
	if (!wl->fw_status)
		goto err;

	wl->tx_res_if = kzalloc(sizeof(*wl->tx_res_if), GFP_KERNEL);
	if (!wl->tx_res_if)
		goto err;

	return 0;
err:
	kfree(wl->fw_status);
	kfree(wl->raw_fw_status);
	return -ENOMEM;
}

static int wl12xx_set_power_on(struct wl1271 *wl)
{
	int ret;

	msleep(WL1271_PRE_POWER_ON_SLEEP);
	ret = wl1271_power_on(wl);
	if (ret < 0)
		goto out;
	msleep(WL1271_POWER_ON_SLEEP);
	wl1271_io_reset(wl);
	wl1271_io_init(wl);

	ret = wlcore_set_partition(wl, &wl->ptable[PART_BOOT]);
	if (ret < 0)
		goto fail;

	/* ELP module wake up */
	ret = wlcore_fw_wakeup(wl);
	if (ret < 0)
		goto fail;

out:
	return ret;

fail:
	wl1271_power_off(wl);
	return ret;
}

static int wl12xx_chip_wakeup(struct wl1271 *wl, bool plt)
{
	int ret = 0;

	ret = wl12xx_set_power_on(wl);
	if (ret < 0)
		goto out;

	/*
	 * For wl127x based devices we could use the default block
	 * size (512 bytes), but due to a bug in the sdio driver, we
	 * need to set it explicitly after the chip is powered on.  To
	 * simplify the code and since the performance impact is
	 * negligible, we use the same block size for all different
	 * chip types.
	 *
	 * Check if the bus supports blocksize alignment and, if it
	 * doesn't, make sure we don't have the quirk.
	 */
	if (!wl1271_set_block_size(wl))
		wl->quirks &= ~WLCORE_QUIRK_TX_BLOCKSIZE_ALIGN;

	/* TODO: make sure the lower driver has set things up correctly */

	ret = wl1271_setup(wl);
	if (ret < 0)
		goto out;

	ret = wl12xx_fetch_firmware(wl, plt);
	if (ret < 0) {
		kfree(wl->fw_status);
		kfree(wl->raw_fw_status);
		kfree(wl->tx_res_if);
	}

out:
	return ret;
}

int wl1271_plt_start(struct wl1271 *wl, const enum plt_mode plt_mode)
{
	int retries = WL1271_BOOT_RETRIES;
	struct wiphy *wiphy = wl->hw->wiphy;

	static const char* const PLT_MODE[] = {
		"PLT_OFF",
		"PLT_ON",
		"PLT_FEM_DETECT",
		"PLT_CHIP_AWAKE"
	};

	int ret;

	mutex_lock(&wl->mutex);

	wl1271_notice("power up");

	if (wl->state != WLCORE_STATE_OFF) {
		wl1271_error("cannot go into PLT state because not "
			     "in off state: %d", wl->state);
		ret = -EBUSY;
		goto out;
	}

	/* Indicate to lower levels that we are now in PLT mode */
	wl->plt = true;
	wl->plt_mode = plt_mode;

	while (retries) {
		retries--;
		ret = wl12xx_chip_wakeup(wl, true);
		if (ret < 0)
			goto power_off;

		if (plt_mode != PLT_CHIP_AWAKE) {
			ret = wl->ops->plt_init(wl);
			if (ret < 0)
				goto power_off;
		}

		wl->state = WLCORE_STATE_ON;
		wl1271_notice("firmware booted in PLT mode %s (%s)",
			      PLT_MODE[plt_mode],
			      wl->chip.fw_ver_str);

		/* update hw/fw version info in wiphy struct */
		wiphy->hw_version = wl->chip.id;
		strncpy(wiphy->fw_version, wl->chip.fw_ver_str,
			sizeof(wiphy->fw_version));

		goto out;

power_off:
		wl1271_power_off(wl);
	}

	wl->plt = false;
	wl->plt_mode = PLT_OFF;

	wl1271_error("firmware boot in PLT mode failed despite %d retries",
		     WL1271_BOOT_RETRIES);
out:
	mutex_unlock(&wl->mutex);

	return ret;
}

int wl1271_plt_stop(struct wl1271 *wl)
{
	int ret = 0;

	wl1271_notice("power down");

	/*
	 * Interrupts must be disabled before setting the state to OFF.
	 * Otherwise, the interrupt handler might be called and exit without
	 * reading the interrupt status.
	 */
	wlcore_disable_interrupts(wl);
	mutex_lock(&wl->mutex);
	if (!wl->plt) {
		mutex_unlock(&wl->mutex);

		/*
		 * This will not necessarily enable interrupts as interrupts
		 * may have been disabled when op_stop was called. It will,
		 * however, balance the above call to disable_interrupts().
		 */
		wlcore_enable_interrupts(wl);

		wl1271_error("cannot power down because not in PLT "
			     "state: %d", wl->state);
		ret = -EBUSY;
		goto out;
	}

	mutex_unlock(&wl->mutex);

	wl1271_flush_deferred_work(wl);
	cancel_work_sync(&wl->netstack_work);
	cancel_work_sync(&wl->recovery_work);
	cancel_delayed_work_sync(&wl->tx_watchdog_work);

	mutex_lock(&wl->mutex);
	wl1271_power_off(wl);
	wl->flags = 0;
	wl->sleep_auth = WL1271_PSM_ILLEGAL;
	wl->state = WLCORE_STATE_OFF;
	wl->plt = false;
	wl->plt_mode = PLT_OFF;
	wl->rx_counter = 0;
	mutex_unlock(&wl->mutex);

out:
	return ret;
}

static void wl1271_op_tx(struct ieee80211_hw *hw,
			 struct ieee80211_tx_control *control,
			 struct sk_buff *skb)
{
	struct wl1271 *wl = hw->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct wl12xx_vif *wlvif = NULL;
	unsigned long flags;
	int q, mapping;
	u8 hlid;

	if (!vif) {
		wl1271_debug(DEBUG_TX, "DROP skb with no vif");
		ieee80211_free_txskb(hw, skb);
		return;
	}

	wlvif = wl12xx_vif_to_data(vif);
	mapping = skb_get_queue_mapping(skb);
	q = wl1271_tx_get_queue(mapping);

	hlid = wl12xx_tx_get_hlid(wl, wlvif, skb, control->sta);

	spin_lock_irqsave(&wl->wl_lock, flags);

	/*
	 * drop the packet if the link is invalid or the queue is stopped
	 * for any reason but watermark. Watermark is a "soft"-stop so we
	 * allow these packets through.
	 */
	if (hlid == WL12XX_INVALID_LINK_ID ||
	    (!test_bit(hlid, wlvif->links_map)) ||
	     (wlcore_is_queue_stopped_locked(wl, wlvif, q) &&
	      !wlcore_is_queue_stopped_by_reason_locked(wl, wlvif, q,
			WLCORE_QUEUE_STOP_REASON_WATERMARK))) {
		wl1271_debug(DEBUG_TX, "DROP skb hlid %d q %d", hlid, q);
		ieee80211_free_txskb(hw, skb);
		goto out;
	}

	wl1271_debug(DEBUG_TX, "queue skb hlid %d q %d len %d",
		     hlid, q, skb->len);
	skb_queue_tail(&wl->links[hlid].tx_queue[q], skb);

	wl->tx_queue_count[q]++;
	wlvif->tx_queue_count[q]++;

	/*
	 * The workqueue is slow to process the tx_queue and we need stop
	 * the queue here, otherwise the queue will get too long.
	 */
	if (wlvif->tx_queue_count[q] >= WL1271_TX_QUEUE_HIGH_WATERMARK &&
	    !wlcore_is_queue_stopped_by_reason_locked(wl, wlvif, q,
					WLCORE_QUEUE_STOP_REASON_WATERMARK)) {
		wl1271_debug(DEBUG_TX, "op_tx: stopping queues for q %d", q);
		wlcore_stop_queue_locked(wl, wlvif, q,
					 WLCORE_QUEUE_STOP_REASON_WATERMARK);
	}

	/*
	 * The chip specific setup must run before the first TX packet -
	 * before that, the tx_work will not be initialized!
	 */

	if (!test_bit(WL1271_FLAG_FW_TX_BUSY, &wl->flags) &&
	    !test_bit(WL1271_FLAG_TX_PENDING, &wl->flags))
		ieee80211_queue_work(wl->hw, &wl->tx_work);

out:
	spin_unlock_irqrestore(&wl->wl_lock, flags);
}

int wl1271_tx_dummy_packet(struct wl1271 *wl)
{
	unsigned long flags;
	int q;

	/* no need to queue a new dummy packet if one is already pending */
	if (test_bit(WL1271_FLAG_DUMMY_PACKET_PENDING, &wl->flags))
		return 0;

	q = wl1271_tx_get_queue(skb_get_queue_mapping(wl->dummy_packet));

	spin_lock_irqsave(&wl->wl_lock, flags);
	set_bit(WL1271_FLAG_DUMMY_PACKET_PENDING, &wl->flags);
	wl->tx_queue_count[q]++;
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	/* The FW is low on RX memory blocks, so send the dummy packet asap */
	if (!test_bit(WL1271_FLAG_FW_TX_BUSY, &wl->flags))
		return wlcore_tx_work_locked(wl);

	/*
	 * If the FW TX is busy, TX work will be scheduled by the threaded
	 * interrupt handler function
	 */
	return 0;
}

/*
 * The size of the dummy packet should be at least 1400 bytes. However, in
 * order to minimize the number of bus transactions, aligning it to 512 bytes
 * boundaries could be beneficial, performance wise
 */
#define TOTAL_TX_DUMMY_PACKET_SIZE (ALIGN(1400, 512))

static struct sk_buff *wl12xx_alloc_dummy_packet(struct wl1271 *wl)
{
	struct sk_buff *skb;
	struct ieee80211_hdr_3addr *hdr;
	unsigned int dummy_packet_size;

	dummy_packet_size = TOTAL_TX_DUMMY_PACKET_SIZE -
			    sizeof(struct wl1271_tx_hw_descr) - sizeof(*hdr);

	skb = dev_alloc_skb(TOTAL_TX_DUMMY_PACKET_SIZE);
	if (!skb) {
		wl1271_warning("Failed to allocate a dummy packet skb");
		return NULL;
	}

	skb_reserve(skb, sizeof(struct wl1271_tx_hw_descr));

	hdr = skb_put_zero(skb, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					 IEEE80211_STYPE_NULLFUNC |
					 IEEE80211_FCTL_TODS);

	skb_put_zero(skb, dummy_packet_size);

	/* Dummy packets require the TID to be management */
	skb->priority = WL1271_TID_MGMT;

	/* Initialize all fields that might be used */
	skb_set_queue_mapping(skb, 0);
	memset(IEEE80211_SKB_CB(skb), 0, sizeof(struct ieee80211_tx_info));

	return skb;
}


static int
wl1271_validate_wowlan_pattern(struct cfg80211_pkt_pattern *p)
{
	int num_fields = 0, in_field = 0, fields_size = 0;
	int i, pattern_len = 0;

	if (!p->mask) {
		wl1271_warning("No mask in WoWLAN pattern");
		return -EINVAL;
	}

	/*
	 * The pattern is broken up into segments of bytes at different offsets
	 * that need to be checked by the FW filter. Each segment is called
	 * a field in the FW API. We verify that the total number of fields
	 * required for this pattern won't exceed FW limits (8)
	 * as well as the total fields buffer won't exceed the FW limit.
	 * Note that if there's a pattern which crosses Ethernet/IP header
	 * boundary a new field is required.
	 */
	for (i = 0; i < p->pattern_len; i++) {
		if (test_bit(i, (unsigned long *)p->mask)) {
			if (!in_field) {
				in_field = 1;
				pattern_len = 1;
			} else {
				if (i == WL1271_RX_FILTER_ETH_HEADER_SIZE) {
					num_fields++;
					fields_size += pattern_len +
						RX_FILTER_FIELD_OVERHEAD;
					pattern_len = 1;
				} else
					pattern_len++;
			}
		} else {
			if (in_field) {
				in_field = 0;
				fields_size += pattern_len +
					RX_FILTER_FIELD_OVERHEAD;
				num_fields++;
			}
		}
	}

	if (in_field) {
		fields_size += pattern_len + RX_FILTER_FIELD_OVERHEAD;
		num_fields++;
	}

	if (num_fields > WL1271_RX_FILTER_MAX_FIELDS) {
		wl1271_warning("RX Filter too complex. Too many segments");
		return -EINVAL;
	}

	if (fields_size > WL1271_RX_FILTER_MAX_FIELDS_SIZE) {
		wl1271_warning("RX filter pattern is too big");
		return -E2BIG;
	}

	return 0;
}

struct wl12xx_rx_filter *wl1271_rx_filter_alloc(void)
{
	return kzalloc(sizeof(struct wl12xx_rx_filter), GFP_KERNEL);
}

void wl1271_rx_filter_free(struct wl12xx_rx_filter *filter)
{
	int i;

	if (filter == NULL)
		return;

	for (i = 0; i < filter->num_fields; i++)
		kfree(filter->fields[i].pattern);

	kfree(filter);
}

int wl1271_rx_filter_alloc_field(struct wl12xx_rx_filter *filter,
				 u16 offset, u8 flags,
				 const u8 *pattern, u8 len)
{
	struct wl12xx_rx_filter_field *field;

	if (filter->num_fields == WL1271_RX_FILTER_MAX_FIELDS) {
		wl1271_warning("Max fields per RX filter. can't alloc another");
		return -EINVAL;
	}

	field = &filter->fields[filter->num_fields];

	field->pattern = kmemdup(pattern, len, GFP_KERNEL);
	if (!field->pattern) {
		wl1271_warning("Failed to allocate RX filter pattern");
		return -ENOMEM;
	}

	filter->num_fields++;

	field->offset = cpu_to_le16(offset);
	field->flags = flags;
	field->len = len;

	return 0;
}

int wl1271_rx_filter_get_fields_size(struct wl12xx_rx_filter *filter)
{
	int i, fields_size = 0;

	for (i = 0; i < filter->num_fields; i++)
		fields_size += filter->fields[i].len +
			sizeof(struct wl12xx_rx_filter_field) -
			sizeof(u8 *);

	return fields_size;
}

void wl1271_rx_filter_flatten_fields(struct wl12xx_rx_filter *filter,
				    u8 *buf)
{
	int i;
	struct wl12xx_rx_filter_field *field;

	for (i = 0; i < filter->num_fields; i++) {
		field = (struct wl12xx_rx_filter_field *)buf;

		field->offset = filter->fields[i].offset;
		field->flags = filter->fields[i].flags;
		field->len = filter->fields[i].len;

		memcpy(&field->pattern, filter->fields[i].pattern, field->len);
		buf += sizeof(struct wl12xx_rx_filter_field) -
			sizeof(u8 *) + field->len;
	}
}

/*
 * Allocates an RX filter returned through f
 * which needs to be freed using rx_filter_free()
 */
static int
wl1271_convert_wowlan_pattern_to_rx_filter(struct cfg80211_pkt_pattern *p,
					   struct wl12xx_rx_filter **f)
{
	int i, j, ret = 0;
	struct wl12xx_rx_filter *filter;
	u16 offset;
	u8 flags, len;

	filter = wl1271_rx_filter_alloc();
	if (!filter) {
		wl1271_warning("Failed to alloc rx filter");
		ret = -ENOMEM;
		goto err;
	}

	i = 0;
	while (i < p->pattern_len) {
		if (!test_bit(i, (unsigned long *)p->mask)) {
			i++;
			continue;
		}

		for (j = i; j < p->pattern_len; j++) {
			if (!test_bit(j, (unsigned long *)p->mask))
				break;

			if (i < WL1271_RX_FILTER_ETH_HEADER_SIZE &&
			    j >= WL1271_RX_FILTER_ETH_HEADER_SIZE)
				break;
		}

		if (i < WL1271_RX_FILTER_ETH_HEADER_SIZE) {
			offset = i;
			flags = WL1271_RX_FILTER_FLAG_ETHERNET_HEADER;
		} else {
			offset = i - WL1271_RX_FILTER_ETH_HEADER_SIZE;
			flags = WL1271_RX_FILTER_FLAG_IP_HEADER;
		}

		len = j - i;

		ret = wl1271_rx_filter_alloc_field(filter,
						   offset,
						   flags,
						   &p->pattern[i], len);
		if (ret)
			goto err;

		i = j;
	}

	filter->action = FILTER_SIGNAL;

	*f = filter;
	return 0;

err:
	wl1271_rx_filter_free(filter);
	*f = NULL;

	return ret;
}

static int wl1271_configure_wowlan(struct wl1271 *wl,
				   struct cfg80211_wowlan *wow)
{
	int i, ret;

	if (!wow || wow->any || !wow->n_patterns) {
		ret = wl1271_acx_default_rx_filter_enable(wl, 0,
							  FILTER_SIGNAL);
		if (ret)
			goto out;

		ret = wl1271_rx_filter_clear_all(wl);
		if (ret)
			goto out;

		return 0;
	}

	if (WARN_ON(wow->n_patterns > WL1271_MAX_RX_FILTERS))
		return -EINVAL;

	/* Validate all incoming patterns before clearing current FW state */
	for (i = 0; i < wow->n_patterns; i++) {
		ret = wl1271_validate_wowlan_pattern(&wow->patterns[i]);
		if (ret) {
			wl1271_warning("Bad wowlan pattern %d", i);
			return ret;
		}
	}

	ret = wl1271_acx_default_rx_filter_enable(wl, 0, FILTER_SIGNAL);
	if (ret)
		goto out;

	ret = wl1271_rx_filter_clear_all(wl);
	if (ret)
		goto out;

	/* Translate WoWLAN patterns into filters */
	for (i = 0; i < wow->n_patterns; i++) {
		struct cfg80211_pkt_pattern *p;
		struct wl12xx_rx_filter *filter = NULL;

		p = &wow->patterns[i];

		ret = wl1271_convert_wowlan_pattern_to_rx_filter(p, &filter);
		if (ret) {
			wl1271_warning("Failed to create an RX filter from "
				       "wowlan pattern %d", i);
			goto out;
		}

		ret = wl1271_rx_filter_enable(wl, i, 1, filter);

		wl1271_rx_filter_free(filter);
		if (ret)
			goto out;
	}

	ret = wl1271_acx_default_rx_filter_enable(wl, 1, FILTER_DROP);

out:
	return ret;
}

static int wl1271_configure_suspend_sta(struct wl1271 *wl,
					struct wl12xx_vif *wlvif,
					struct cfg80211_wowlan *wow)
{
	int ret = 0;

	if (!test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
		goto out;

	ret = wl1271_configure_wowlan(wl, wow);
	if (ret < 0)
		goto out;

	if ((wl->conf.conn.suspend_wake_up_event ==
	     wl->conf.conn.wake_up_event) &&
	    (wl->conf.conn.suspend_listen_interval ==
	     wl->conf.conn.listen_interval))
		goto out;

	ret = wl1271_acx_wake_up_conditions(wl, wlvif,
				    wl->conf.conn.suspend_wake_up_event,
				    wl->conf.conn.suspend_listen_interval);

	if (ret < 0)
		wl1271_error("suspend: set wake up conditions failed: %d", ret);
out:
	return ret;

}

static int wl1271_configure_suspend_ap(struct wl1271 *wl,
					struct wl12xx_vif *wlvif,
					struct cfg80211_wowlan *wow)
{
	int ret = 0;

	if (!test_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags))
		goto out;

	ret = wl1271_acx_beacon_filter_opt(wl, wlvif, true);
	if (ret < 0)
		goto out;

	ret = wl1271_configure_wowlan(wl, wow);
	if (ret < 0)
		goto out;

out:
	return ret;

}

static int wl1271_configure_suspend(struct wl1271 *wl,
				    struct wl12xx_vif *wlvif,
				    struct cfg80211_wowlan *wow)
{
	if (wlvif->bss_type == BSS_TYPE_STA_BSS)
		return wl1271_configure_suspend_sta(wl, wlvif, wow);
	if (wlvif->bss_type == BSS_TYPE_AP_BSS)
		return wl1271_configure_suspend_ap(wl, wlvif, wow);
	return 0;
}

static void wl1271_configure_resume(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	int ret = 0;
	bool is_ap = wlvif->bss_type == BSS_TYPE_AP_BSS;
	bool is_sta = wlvif->bss_type == BSS_TYPE_STA_BSS;

	if ((!is_ap) && (!is_sta))
		return;

	if ((is_sta && !test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags)) ||
	    (is_ap && !test_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags)))
		return;

	wl1271_configure_wowlan(wl, NULL);

	if (is_sta) {
		if ((wl->conf.conn.suspend_wake_up_event ==
		     wl->conf.conn.wake_up_event) &&
		    (wl->conf.conn.suspend_listen_interval ==
		     wl->conf.conn.listen_interval))
			return;

		ret = wl1271_acx_wake_up_conditions(wl, wlvif,
				    wl->conf.conn.wake_up_event,
				    wl->conf.conn.listen_interval);

		if (ret < 0)
			wl1271_error("resume: wake up conditions failed: %d",
				     ret);

	} else if (is_ap) {
		ret = wl1271_acx_beacon_filter_opt(wl, wlvif, false);
	}
}

static int __maybe_unused wl1271_op_suspend(struct ieee80211_hw *hw,
					    struct cfg80211_wowlan *wow)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif;
	unsigned long flags;
	int ret;

	wl1271_debug(DEBUG_MAC80211, "mac80211 suspend wow=%d", !!wow);
	WARN_ON(!wow);

	/* we want to perform the recovery before suspending */
	if (test_bit(WL1271_FLAG_RECOVERY_IN_PROGRESS, &wl->flags)) {
		wl1271_warning("postponing suspend to perform recovery");
		return -EBUSY;
	}

	wl1271_tx_flush(wl);

	mutex_lock(&wl->mutex);

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		mutex_unlock(&wl->mutex);
		return ret;
	}

	wl->wow_enabled = true;
	wl12xx_for_each_wlvif(wl, wlvif) {
		if (wlcore_is_p2p_mgmt(wlvif))
			continue;

		ret = wl1271_configure_suspend(wl, wlvif, wow);
		if (ret < 0) {
			mutex_unlock(&wl->mutex);
			wl1271_warning("couldn't prepare device to suspend");
			return ret;
		}
	}

	/* disable fast link flow control notifications from FW */
	ret = wlcore_hw_interrupt_notify(wl, false);
	if (ret < 0)
		goto out_sleep;

	/* if filtering is enabled, configure the FW to drop all RX BA frames */
	ret = wlcore_hw_rx_ba_filter(wl,
				     !!wl->conf.conn.suspend_rx_ba_activity);
	if (ret < 0)
		goto out_sleep;

out_sleep:
	pm_runtime_put_noidle(wl->dev);
	mutex_unlock(&wl->mutex);

	if (ret < 0) {
		wl1271_warning("couldn't prepare device to suspend");
		return ret;
	}

	/* flush any remaining work */
	wl1271_debug(DEBUG_MAC80211, "flushing remaining works");

	flush_work(&wl->tx_work);

	/*
	 * Cancel the watchdog even if above tx_flush failed. We will detect
	 * it on resume anyway.
	 */
	cancel_delayed_work(&wl->tx_watchdog_work);

	/*
	 * set suspended flag to avoid triggering a new threaded_irq
	 * work.
	 */
	spin_lock_irqsave(&wl->wl_lock, flags);
	set_bit(WL1271_FLAG_SUSPENDED, &wl->flags);
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	return pm_runtime_force_suspend(wl->dev);
}

static int __maybe_unused wl1271_op_resume(struct ieee80211_hw *hw)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif;
	unsigned long flags;
	bool run_irq_work = false, pending_recovery;
	int ret;

	wl1271_debug(DEBUG_MAC80211, "mac80211 resume wow=%d",
		     wl->wow_enabled);
	WARN_ON(!wl->wow_enabled);

	ret = pm_runtime_force_resume(wl->dev);
	if (ret < 0) {
		wl1271_error("ELP wakeup failure!");
		goto out_sleep;
	}

	/*
	 * re-enable irq_work enqueuing, and call irq_work directly if
	 * there is a pending work.
	 */
	spin_lock_irqsave(&wl->wl_lock, flags);
	clear_bit(WL1271_FLAG_SUSPENDED, &wl->flags);
	if (test_and_clear_bit(WL1271_FLAG_PENDING_WORK, &wl->flags))
		run_irq_work = true;
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	mutex_lock(&wl->mutex);

	/* test the recovery flag before calling any SDIO functions */
	pending_recovery = test_bit(WL1271_FLAG_RECOVERY_IN_PROGRESS,
				    &wl->flags);

	if (run_irq_work) {
		wl1271_debug(DEBUG_MAC80211,
			     "run postponed irq_work directly");

		/* don't talk to the HW if recovery is pending */
		if (!pending_recovery) {
			ret = wlcore_irq_locked(wl);
			if (ret)
				wl12xx_queue_recovery_work(wl);
		}

		wlcore_enable_interrupts(wl);
	}

	if (pending_recovery) {
		wl1271_warning("queuing forgotten recovery on resume");
		ieee80211_queue_work(wl->hw, &wl->recovery_work);
		goto out_sleep;
	}

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	wl12xx_for_each_wlvif(wl, wlvif) {
		if (wlcore_is_p2p_mgmt(wlvif))
			continue;

		wl1271_configure_resume(wl, wlvif);
	}

	ret = wlcore_hw_interrupt_notify(wl, true);
	if (ret < 0)
		goto out_sleep;

	/* if filtering is enabled, configure the FW to drop all RX BA frames */
	ret = wlcore_hw_rx_ba_filter(wl, false);
	if (ret < 0)
		goto out_sleep;

out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

out:
	wl->wow_enabled = false;

	/*
	 * Set a flag to re-init the watchdog on the first Tx after resume.
	 * That way we avoid possible conditions where Tx-complete interrupts
	 * fail to arrive and we perform a spurious recovery.
	 */
	set_bit(WL1271_FLAG_REINIT_TX_WDOG, &wl->flags);
	mutex_unlock(&wl->mutex);

	return 0;
}

static int wl1271_op_start(struct ieee80211_hw *hw)
{
	wl1271_debug(DEBUG_MAC80211, "mac80211 start");

	/*
	 * We have to delay the booting of the hardware because
	 * we need to know the local MAC address before downloading and
	 * initializing the firmware. The MAC address cannot be changed
	 * after boot, and without the proper MAC address, the firmware
	 * will not function properly.
	 *
	 * The MAC address is first known when the corresponding interface
	 * is added. That is where we will initialize the hardware.
	 */

	return 0;
}

static void wlcore_op_stop_locked(struct wl1271 *wl)
{
	int i;

	if (wl->state == WLCORE_STATE_OFF) {
		if (test_and_clear_bit(WL1271_FLAG_RECOVERY_IN_PROGRESS,
					&wl->flags))
			wlcore_enable_interrupts(wl);

		return;
	}

	/*
	 * this must be before the cancel_work calls below, so that the work
	 * functions don't perform further work.
	 */
	wl->state = WLCORE_STATE_OFF;

	/*
	 * Use the nosync variant to disable interrupts, so the mutex could be
	 * held while doing so without deadlocking.
	 */
	wlcore_disable_interrupts_nosync(wl);

	mutex_unlock(&wl->mutex);

	wlcore_synchronize_interrupts(wl);
	if (!test_bit(WL1271_FLAG_RECOVERY_IN_PROGRESS, &wl->flags))
		cancel_work_sync(&wl->recovery_work);
	wl1271_flush_deferred_work(wl);
	cancel_delayed_work_sync(&wl->scan_complete_work);
	cancel_work_sync(&wl->netstack_work);
	cancel_work_sync(&wl->tx_work);
	cancel_delayed_work_sync(&wl->tx_watchdog_work);

	/* let's notify MAC80211 about the remaining pending TX frames */
	mutex_lock(&wl->mutex);
	wl12xx_tx_reset(wl);

	wl1271_power_off(wl);
	/*
	 * In case a recovery was scheduled, interrupts were disabled to avoid
	 * an interrupt storm. Now that the power is down, it is safe to
	 * re-enable interrupts to balance the disable depth
	 */
	if (test_and_clear_bit(WL1271_FLAG_RECOVERY_IN_PROGRESS, &wl->flags))
		wlcore_enable_interrupts(wl);

	wl->band = NL80211_BAND_2GHZ;

	wl->rx_counter = 0;
	wl->power_level = WL1271_DEFAULT_POWER_LEVEL;
	wl->channel_type = NL80211_CHAN_NO_HT;
	wl->tx_blocks_available = 0;
	wl->tx_allocated_blocks = 0;
	wl->tx_results_count = 0;
	wl->tx_packets_count = 0;
	wl->time_offset = 0;
	wl->ap_fw_ps_map = 0;
	wl->ap_ps_map = 0;
	wl->sleep_auth = WL1271_PSM_ILLEGAL;
	memset(wl->roles_map, 0, sizeof(wl->roles_map));
	memset(wl->links_map, 0, sizeof(wl->links_map));
	memset(wl->roc_map, 0, sizeof(wl->roc_map));
	memset(wl->session_ids, 0, sizeof(wl->session_ids));
	memset(wl->rx_filter_enabled, 0, sizeof(wl->rx_filter_enabled));
	wl->active_sta_count = 0;
	wl->active_link_count = 0;

	/* The system link is always allocated */
	wl->links[WL12XX_SYSTEM_HLID].allocated_pkts = 0;
	wl->links[WL12XX_SYSTEM_HLID].prev_freed_pkts = 0;
	__set_bit(WL12XX_SYSTEM_HLID, wl->links_map);

	/*
	 * this is performed after the cancel_work calls and the associated
	 * mutex_lock, so that wl1271_op_add_interface does not accidentally
	 * get executed before all these vars have been reset.
	 */
	wl->flags = 0;

	wl->tx_blocks_freed = 0;

	for (i = 0; i < NUM_TX_QUEUES; i++) {
		wl->tx_pkts_freed[i] = 0;
		wl->tx_allocated_pkts[i] = 0;
	}

	wl1271_debugfs_reset(wl);

	kfree(wl->raw_fw_status);
	wl->raw_fw_status = NULL;
	kfree(wl->fw_status);
	wl->fw_status = NULL;
	kfree(wl->tx_res_if);
	wl->tx_res_if = NULL;
	kfree(wl->target_mem_map);
	wl->target_mem_map = NULL;

	/*
	 * FW channels must be re-calibrated after recovery,
	 * save current Reg-Domain channel configuration and clear it.
	 */
	memcpy(wl->reg_ch_conf_pending, wl->reg_ch_conf_last,
	       sizeof(wl->reg_ch_conf_pending));
	memset(wl->reg_ch_conf_last, 0, sizeof(wl->reg_ch_conf_last));
}

static void wlcore_op_stop(struct ieee80211_hw *hw)
{
	struct wl1271 *wl = hw->priv;

	wl1271_debug(DEBUG_MAC80211, "mac80211 stop");

	mutex_lock(&wl->mutex);

	wlcore_op_stop_locked(wl);

	mutex_unlock(&wl->mutex);
}

static void wlcore_channel_switch_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wl1271 *wl;
	struct ieee80211_vif *vif;
	struct wl12xx_vif *wlvif;
	int ret;

	dwork = to_delayed_work(work);
	wlvif = container_of(dwork, struct wl12xx_vif, channel_switch_work);
	wl = wlvif->wl;

	wl1271_info("channel switch failed (role_id: %d).", wlvif->role_id);

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	/* check the channel switch is still ongoing */
	if (!test_and_clear_bit(WLVIF_FLAG_CS_PROGRESS, &wlvif->flags))
		goto out;

	vif = wl12xx_wlvif_to_vif(wlvif);
	ieee80211_chswitch_done(vif, false);

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	wl12xx_cmd_stop_channel_switch(wl, wlvif);

	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);
}

static void wlcore_connection_loss_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wl1271 *wl;
	struct ieee80211_vif *vif;
	struct wl12xx_vif *wlvif;

	dwork = to_delayed_work(work);
	wlvif = container_of(dwork, struct wl12xx_vif, connection_loss_work);
	wl = wlvif->wl;

	wl1271_info("Connection loss work (role_id: %d).", wlvif->role_id);

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	/* Call mac80211 connection loss */
	if (!test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
		goto out;

	vif = wl12xx_wlvif_to_vif(wlvif);
	ieee80211_connection_loss(vif);
out:
	mutex_unlock(&wl->mutex);
}

static void wlcore_pending_auth_complete_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wl1271 *wl;
	struct wl12xx_vif *wlvif;
	unsigned long time_spare;
	int ret;

	dwork = to_delayed_work(work);
	wlvif = container_of(dwork, struct wl12xx_vif,
			     pending_auth_complete_work);
	wl = wlvif->wl;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	/*
	 * Make sure a second really passed since the last auth reply. Maybe
	 * a second auth reply arrived while we were stuck on the mutex.
	 * Check for a little less than the timeout to protect from scheduler
	 * irregularities.
	 */
	time_spare = jiffies +
			msecs_to_jiffies(WLCORE_PEND_AUTH_ROC_TIMEOUT - 50);
	if (!time_after(time_spare, wlvif->pending_auth_reply_time))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	/* cancel the ROC if active */
	wlcore_update_inconn_sta(wl, wlvif, NULL, false);

	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);
}

static int wl12xx_allocate_rate_policy(struct wl1271 *wl, u8 *idx)
{
	u8 policy = find_first_zero_bit(wl->rate_policies_map,
					WL12XX_MAX_RATE_POLICIES);
	if (policy >= WL12XX_MAX_RATE_POLICIES)
		return -EBUSY;

	__set_bit(policy, wl->rate_policies_map);
	*idx = policy;
	return 0;
}

static void wl12xx_free_rate_policy(struct wl1271 *wl, u8 *idx)
{
	if (WARN_ON(*idx >= WL12XX_MAX_RATE_POLICIES))
		return;

	__clear_bit(*idx, wl->rate_policies_map);
	*idx = WL12XX_MAX_RATE_POLICIES;
}

static int wlcore_allocate_klv_template(struct wl1271 *wl, u8 *idx)
{
	u8 policy = find_first_zero_bit(wl->klv_templates_map,
					WLCORE_MAX_KLV_TEMPLATES);
	if (policy >= WLCORE_MAX_KLV_TEMPLATES)
		return -EBUSY;

	__set_bit(policy, wl->klv_templates_map);
	*idx = policy;
	return 0;
}

static void wlcore_free_klv_template(struct wl1271 *wl, u8 *idx)
{
	if (WARN_ON(*idx >= WLCORE_MAX_KLV_TEMPLATES))
		return;

	__clear_bit(*idx, wl->klv_templates_map);
	*idx = WLCORE_MAX_KLV_TEMPLATES;
}

static u8 wl12xx_get_role_type(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);

	switch (wlvif->bss_type) {
	case BSS_TYPE_AP_BSS:
		if (wlvif->p2p)
			return WL1271_ROLE_P2P_GO;
		else if (ieee80211_vif_is_mesh(vif))
			return WL1271_ROLE_MESH_POINT;
		else
			return WL1271_ROLE_AP;

	case BSS_TYPE_STA_BSS:
		if (wlvif->p2p)
			return WL1271_ROLE_P2P_CL;
		else
			return WL1271_ROLE_STA;

	case BSS_TYPE_IBSS:
		return WL1271_ROLE_IBSS;

	default:
		wl1271_error("invalid bss_type: %d", wlvif->bss_type);
	}
	return WL12XX_INVALID_ROLE_TYPE;
}

static int wl12xx_init_vif_data(struct wl1271 *wl, struct ieee80211_vif *vif)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	int i;

	/* clear everything but the persistent data */
	memset(wlvif, 0, offsetof(struct wl12xx_vif, persistent));

	switch (ieee80211_vif_type_p2p(vif)) {
	case NL80211_IFTYPE_P2P_CLIENT:
		wlvif->p2p = 1;
		/* fall-through */
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_DEVICE:
		wlvif->bss_type = BSS_TYPE_STA_BSS;
		break;
	case NL80211_IFTYPE_ADHOC:
		wlvif->bss_type = BSS_TYPE_IBSS;
		break;
	case NL80211_IFTYPE_P2P_GO:
		wlvif->p2p = 1;
		/* fall-through */
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		wlvif->bss_type = BSS_TYPE_AP_BSS;
		break;
	default:
		wlvif->bss_type = MAX_BSS_TYPE;
		return -EOPNOTSUPP;
	}

	wlvif->role_id = WL12XX_INVALID_ROLE_ID;
	wlvif->dev_role_id = WL12XX_INVALID_ROLE_ID;
	wlvif->dev_hlid = WL12XX_INVALID_LINK_ID;

	if (wlvif->bss_type == BSS_TYPE_STA_BSS ||
	    wlvif->bss_type == BSS_TYPE_IBSS) {
		/* init sta/ibss data */
		wlvif->sta.hlid = WL12XX_INVALID_LINK_ID;
		wl12xx_allocate_rate_policy(wl, &wlvif->sta.basic_rate_idx);
		wl12xx_allocate_rate_policy(wl, &wlvif->sta.ap_rate_idx);
		wl12xx_allocate_rate_policy(wl, &wlvif->sta.p2p_rate_idx);
		wlcore_allocate_klv_template(wl, &wlvif->sta.klv_template_id);
		wlvif->basic_rate_set = CONF_TX_RATE_MASK_BASIC;
		wlvif->basic_rate = CONF_TX_RATE_MASK_BASIC;
		wlvif->rate_set = CONF_TX_RATE_MASK_BASIC;
	} else {
		/* init ap data */
		wlvif->ap.bcast_hlid = WL12XX_INVALID_LINK_ID;
		wlvif->ap.global_hlid = WL12XX_INVALID_LINK_ID;
		wl12xx_allocate_rate_policy(wl, &wlvif->ap.mgmt_rate_idx);
		wl12xx_allocate_rate_policy(wl, &wlvif->ap.bcast_rate_idx);
		for (i = 0; i < CONF_TX_MAX_AC_COUNT; i++)
			wl12xx_allocate_rate_policy(wl,
						&wlvif->ap.ucast_rate_idx[i]);
		wlvif->basic_rate_set = CONF_TX_ENABLED_RATES;
		/*
		 * TODO: check if basic_rate shouldn't be
		 * wl1271_tx_min_rate_get(wl, wlvif->basic_rate_set);
		 * instead (the same thing for STA above).
		*/
		wlvif->basic_rate = CONF_TX_ENABLED_RATES;
		/* TODO: this seems to be used only for STA, check it */
		wlvif->rate_set = CONF_TX_ENABLED_RATES;
	}

	wlvif->bitrate_masks[NL80211_BAND_2GHZ] = wl->conf.tx.basic_rate;
	wlvif->bitrate_masks[NL80211_BAND_5GHZ] = wl->conf.tx.basic_rate_5;
	wlvif->beacon_int = WL1271_DEFAULT_BEACON_INT;

	/*
	 * mac80211 configures some values globally, while we treat them
	 * per-interface. thus, on init, we have to copy them from wl
	 */
	wlvif->band = wl->band;
	wlvif->channel = wl->channel;
	wlvif->power_level = wl->power_level;
	wlvif->channel_type = wl->channel_type;

	INIT_WORK(&wlvif->rx_streaming_enable_work,
		  wl1271_rx_streaming_enable_work);
	INIT_WORK(&wlvif->rx_streaming_disable_work,
		  wl1271_rx_streaming_disable_work);
	INIT_WORK(&wlvif->rc_update_work, wlcore_rc_update_work);
	INIT_DELAYED_WORK(&wlvif->channel_switch_work,
			  wlcore_channel_switch_work);
	INIT_DELAYED_WORK(&wlvif->connection_loss_work,
			  wlcore_connection_loss_work);
	INIT_DELAYED_WORK(&wlvif->pending_auth_complete_work,
			  wlcore_pending_auth_complete_work);
	INIT_LIST_HEAD(&wlvif->list);

	timer_setup(&wlvif->rx_streaming_timer, wl1271_rx_streaming_timer, 0);
	return 0;
}

static int wl12xx_init_fw(struct wl1271 *wl)
{
	int retries = WL1271_BOOT_RETRIES;
	bool booted = false;
	struct wiphy *wiphy = wl->hw->wiphy;
	int ret;

	while (retries) {
		retries--;
		ret = wl12xx_chip_wakeup(wl, false);
		if (ret < 0)
			goto power_off;

		ret = wl->ops->boot(wl);
		if (ret < 0)
			goto power_off;

		ret = wl1271_hw_init(wl);
		if (ret < 0)
			goto irq_disable;

		booted = true;
		break;

irq_disable:
		mutex_unlock(&wl->mutex);
		/* Unlocking the mutex in the middle of handling is
		   inherently unsafe. In this case we deem it safe to do,
		   because we need to let any possibly pending IRQ out of
		   the system (and while we are WLCORE_STATE_OFF the IRQ
		   work function will not do anything.) Also, any other
		   possible concurrent operations will fail due to the
		   current state, hence the wl1271 struct should be safe. */
		wlcore_disable_interrupts(wl);
		wl1271_flush_deferred_work(wl);
		cancel_work_sync(&wl->netstack_work);
		mutex_lock(&wl->mutex);
power_off:
		wl1271_power_off(wl);
	}

	if (!booted) {
		wl1271_error("firmware boot failed despite %d retries",
			     WL1271_BOOT_RETRIES);
		goto out;
	}

	wl1271_info("firmware booted (%s)", wl->chip.fw_ver_str);

	/* update hw/fw version info in wiphy struct */
	wiphy->hw_version = wl->chip.id;
	strncpy(wiphy->fw_version, wl->chip.fw_ver_str,
		sizeof(wiphy->fw_version));

	/*
	 * Now we know if 11a is supported (info from the NVS), so disable
	 * 11a channels if not supported
	 */
	if (!wl->enable_11a)
		wiphy->bands[NL80211_BAND_5GHZ]->n_channels = 0;

	wl1271_debug(DEBUG_MAC80211, "11a is %ssupported",
		     wl->enable_11a ? "" : "not ");

	wl->state = WLCORE_STATE_ON;
out:
	return ret;
}

static bool wl12xx_dev_role_started(struct wl12xx_vif *wlvif)
{
	return wlvif->dev_hlid != WL12XX_INVALID_LINK_ID;
}

/*
 * Check whether a fw switch (i.e. moving from one loaded
 * fw to another) is needed. This function is also responsible
 * for updating wl->last_vif_count, so it must be called before
 * loading a non-plt fw (so the correct fw (single-role/multi-role)
 * will be used).
 */
static bool wl12xx_need_fw_change(struct wl1271 *wl,
				  struct vif_counter_data vif_counter_data,
				  bool add)
{
	enum wl12xx_fw_type current_fw = wl->fw_type;
	u8 vif_count = vif_counter_data.counter;

	if (test_bit(WL1271_FLAG_VIF_CHANGE_IN_PROGRESS, &wl->flags))
		return false;

	/* increase the vif count if this is a new vif */
	if (add && !vif_counter_data.cur_vif_running)
		vif_count++;

	wl->last_vif_count = vif_count;

	/* no need for fw change if the device is OFF */
	if (wl->state == WLCORE_STATE_OFF)
		return false;

	/* no need for fw change if a single fw is used */
	if (!wl->mr_fw_name)
		return false;

	if (vif_count > 1 && current_fw == WL12XX_FW_TYPE_NORMAL)
		return true;
	if (vif_count <= 1 && current_fw == WL12XX_FW_TYPE_MULTI)
		return true;

	return false;
}

/*
 * Enter "forced psm". Make sure the sta is in psm against the ap,
 * to make the fw switch a bit more disconnection-persistent.
 */
static void wl12xx_force_active_psm(struct wl1271 *wl)
{
	struct wl12xx_vif *wlvif;

	wl12xx_for_each_wlvif_sta(wl, wlvif) {
		wl1271_ps_set_mode(wl, wlvif, STATION_POWER_SAVE_MODE);
	}
}

struct wlcore_hw_queue_iter_data {
	unsigned long hw_queue_map[BITS_TO_LONGS(WLCORE_NUM_MAC_ADDRESSES)];
	/* current vif */
	struct ieee80211_vif *vif;
	/* is the current vif among those iterated */
	bool cur_running;
};

static void wlcore_hw_queue_iter(void *data, u8 *mac,
				 struct ieee80211_vif *vif)
{
	struct wlcore_hw_queue_iter_data *iter_data = data;

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE ||
	    WARN_ON_ONCE(vif->hw_queue[0] == IEEE80211_INVAL_HW_QUEUE))
		return;

	if (iter_data->cur_running || vif == iter_data->vif) {
		iter_data->cur_running = true;
		return;
	}

	__set_bit(vif->hw_queue[0] / NUM_TX_QUEUES, iter_data->hw_queue_map);
}

static int wlcore_allocate_hw_queue_base(struct wl1271 *wl,
					 struct wl12xx_vif *wlvif)
{
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);
	struct wlcore_hw_queue_iter_data iter_data = {};
	int i, q_base;

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		vif->cab_queue = IEEE80211_INVAL_HW_QUEUE;
		return 0;
	}

	iter_data.vif = vif;

	/* mark all bits taken by active interfaces */
	ieee80211_iterate_active_interfaces_atomic(wl->hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					wlcore_hw_queue_iter, &iter_data);

	/* the current vif is already running in mac80211 (resume/recovery) */
	if (iter_data.cur_running) {
		wlvif->hw_queue_base = vif->hw_queue[0];
		wl1271_debug(DEBUG_MAC80211,
			     "using pre-allocated hw queue base %d",
			     wlvif->hw_queue_base);

		/* interface type might have changed type */
		goto adjust_cab_queue;
	}

	q_base = find_first_zero_bit(iter_data.hw_queue_map,
				     WLCORE_NUM_MAC_ADDRESSES);
	if (q_base >= WLCORE_NUM_MAC_ADDRESSES)
		return -EBUSY;

	wlvif->hw_queue_base = q_base * NUM_TX_QUEUES;
	wl1271_debug(DEBUG_MAC80211, "allocating hw queue base: %d",
		     wlvif->hw_queue_base);

	for (i = 0; i < NUM_TX_QUEUES; i++) {
		wl->queue_stop_reasons[wlvif->hw_queue_base + i] = 0;
		/* register hw queues in mac80211 */
		vif->hw_queue[i] = wlvif->hw_queue_base + i;
	}

adjust_cab_queue:
	/* the last places are reserved for cab queues per interface */
	if (wlvif->bss_type == BSS_TYPE_AP_BSS)
		vif->cab_queue = NUM_TX_QUEUES * WLCORE_NUM_MAC_ADDRESSES +
				 wlvif->hw_queue_base / NUM_TX_QUEUES;
	else
		vif->cab_queue = IEEE80211_INVAL_HW_QUEUE;

	return 0;
}

static int wl1271_op_add_interface(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	struct vif_counter_data vif_count;
	int ret = 0;
	u8 role_type;

	if (wl->plt) {
		wl1271_error("Adding Interface not allowed while in PLT mode");
		return -EBUSY;
	}

	vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER |
			     IEEE80211_VIF_SUPPORTS_UAPSD |
			     IEEE80211_VIF_SUPPORTS_CQM_RSSI;

	wl1271_debug(DEBUG_MAC80211, "mac80211 add interface type %d mac %pM",
		     ieee80211_vif_type_p2p(vif), vif->addr);

	wl12xx_get_vif_count(hw, vif, &vif_count);

	mutex_lock(&wl->mutex);

	/*
	 * in some very corner case HW recovery scenarios its possible to
	 * get here before __wl1271_op_remove_interface is complete, so
	 * opt out if that is the case.
	 */
	if (test_bit(WL1271_FLAG_RECOVERY_IN_PROGRESS, &wl->flags) ||
	    test_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags)) {
		ret = -EBUSY;
		goto out;
	}


	ret = wl12xx_init_vif_data(wl, vif);
	if (ret < 0)
		goto out;

	wlvif->wl = wl;
	role_type = wl12xx_get_role_type(wl, wlvif);
	if (role_type == WL12XX_INVALID_ROLE_TYPE) {
		ret = -EINVAL;
		goto out;
	}

	ret = wlcore_allocate_hw_queue_base(wl, wlvif);
	if (ret < 0)
		goto out;

	/*
	 * TODO: after the nvs issue will be solved, move this block
	 * to start(), and make sure here the driver is ON.
	 */
	if (wl->state == WLCORE_STATE_OFF) {
		/*
		 * we still need this in order to configure the fw
		 * while uploading the nvs
		 */
		memcpy(wl->addresses[0].addr, vif->addr, ETH_ALEN);

		ret = wl12xx_init_fw(wl);
		if (ret < 0)
			goto out;
	}

	/*
	 * Call runtime PM only after possible wl12xx_init_fw() above
	 * is done. Otherwise we do not have interrupts enabled.
	 */
	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out_unlock;
	}

	if (wl12xx_need_fw_change(wl, vif_count, true)) {
		wl12xx_force_active_psm(wl);
		set_bit(WL1271_FLAG_INTENDED_FW_RECOVERY, &wl->flags);
		mutex_unlock(&wl->mutex);
		wl1271_recovery_work(&wl->recovery_work);
		return 0;
	}

	if (!wlcore_is_p2p_mgmt(wlvif)) {
		ret = wl12xx_cmd_role_enable(wl, vif->addr,
					     role_type, &wlvif->role_id);
		if (ret < 0)
			goto out;

		ret = wl1271_init_vif_specific(wl, vif);
		if (ret < 0)
			goto out;

	} else {
		ret = wl12xx_cmd_role_enable(wl, vif->addr, WL1271_ROLE_DEVICE,
					     &wlvif->dev_role_id);
		if (ret < 0)
			goto out;

		/* needed mainly for configuring rate policies */
		ret = wl1271_sta_hw_init(wl, wlvif);
		if (ret < 0)
			goto out;
	}

	list_add(&wlvif->list, &wl->wlvif_list);
	set_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags);

	if (wlvif->bss_type == BSS_TYPE_AP_BSS)
		wl->ap_count++;
	else
		wl->sta_count++;
out:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out_unlock:
	mutex_unlock(&wl->mutex);

	return ret;
}

static void __wl1271_op_remove_interface(struct wl1271 *wl,
					 struct ieee80211_vif *vif,
					 bool reset_tx_queues)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	int i, ret;
	bool is_ap = (wlvif->bss_type == BSS_TYPE_AP_BSS);

	wl1271_debug(DEBUG_MAC80211, "mac80211 remove interface");

	if (!test_and_clear_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags))
		return;

	/* because of hardware recovery, we may get here twice */
	if (wl->state == WLCORE_STATE_OFF)
		return;

	wl1271_info("down");

	if (wl->scan.state != WL1271_SCAN_STATE_IDLE &&
	    wl->scan_wlvif == wlvif) {
		struct cfg80211_scan_info info = {
			.aborted = true,
		};

		/*
		 * Rearm the tx watchdog just before idling scan. This
		 * prevents just-finished scans from triggering the watchdog
		 */
		wl12xx_rearm_tx_watchdog_locked(wl);

		wl->scan.state = WL1271_SCAN_STATE_IDLE;
		memset(wl->scan.scanned_ch, 0, sizeof(wl->scan.scanned_ch));
		wl->scan_wlvif = NULL;
		wl->scan.req = NULL;
		ieee80211_scan_completed(wl->hw, &info);
	}

	if (wl->sched_vif == wlvif)
		wl->sched_vif = NULL;

	if (wl->roc_vif == vif) {
		wl->roc_vif = NULL;
		ieee80211_remain_on_channel_expired(wl->hw);
	}

	if (!test_bit(WL1271_FLAG_RECOVERY_IN_PROGRESS, &wl->flags)) {
		/* disable active roles */
		ret = pm_runtime_get_sync(wl->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(wl->dev);
			goto deinit;
		}

		if (wlvif->bss_type == BSS_TYPE_STA_BSS ||
		    wlvif->bss_type == BSS_TYPE_IBSS) {
			if (wl12xx_dev_role_started(wlvif))
				wl12xx_stop_dev(wl, wlvif);
		}

		if (!wlcore_is_p2p_mgmt(wlvif)) {
			ret = wl12xx_cmd_role_disable(wl, &wlvif->role_id);
			if (ret < 0)
				goto deinit;
		} else {
			ret = wl12xx_cmd_role_disable(wl, &wlvif->dev_role_id);
			if (ret < 0)
				goto deinit;
		}

		pm_runtime_mark_last_busy(wl->dev);
		pm_runtime_put_autosuspend(wl->dev);
	}
deinit:
	wl12xx_tx_reset_wlvif(wl, wlvif);

	/* clear all hlids (except system_hlid) */
	wlvif->dev_hlid = WL12XX_INVALID_LINK_ID;

	if (wlvif->bss_type == BSS_TYPE_STA_BSS ||
	    wlvif->bss_type == BSS_TYPE_IBSS) {
		wlvif->sta.hlid = WL12XX_INVALID_LINK_ID;
		wl12xx_free_rate_policy(wl, &wlvif->sta.basic_rate_idx);
		wl12xx_free_rate_policy(wl, &wlvif->sta.ap_rate_idx);
		wl12xx_free_rate_policy(wl, &wlvif->sta.p2p_rate_idx);
		wlcore_free_klv_template(wl, &wlvif->sta.klv_template_id);
	} else {
		wlvif->ap.bcast_hlid = WL12XX_INVALID_LINK_ID;
		wlvif->ap.global_hlid = WL12XX_INVALID_LINK_ID;
		wl12xx_free_rate_policy(wl, &wlvif->ap.mgmt_rate_idx);
		wl12xx_free_rate_policy(wl, &wlvif->ap.bcast_rate_idx);
		for (i = 0; i < CONF_TX_MAX_AC_COUNT; i++)
			wl12xx_free_rate_policy(wl,
						&wlvif->ap.ucast_rate_idx[i]);
		wl1271_free_ap_keys(wl, wlvif);
	}

	dev_kfree_skb(wlvif->probereq);
	wlvif->probereq = NULL;
	if (wl->last_wlvif == wlvif)
		wl->last_wlvif = NULL;
	list_del(&wlvif->list);
	memset(wlvif->ap.sta_hlid_map, 0, sizeof(wlvif->ap.sta_hlid_map));
	wlvif->role_id = WL12XX_INVALID_ROLE_ID;
	wlvif->dev_role_id = WL12XX_INVALID_ROLE_ID;

	if (is_ap)
		wl->ap_count--;
	else
		wl->sta_count--;

	/*
	 * Last AP, have more stations. Configure sleep auth according to STA.
	 * Don't do thin on unintended recovery.
	 */
	if (test_bit(WL1271_FLAG_RECOVERY_IN_PROGRESS, &wl->flags) &&
	    !test_bit(WL1271_FLAG_INTENDED_FW_RECOVERY, &wl->flags))
		goto unlock;

	if (wl->ap_count == 0 && is_ap) {
		/* mask ap events */
		wl->event_mask &= ~wl->ap_event_mask;
		wl1271_event_unmask(wl);
	}

	if (wl->ap_count == 0 && is_ap && wl->sta_count) {
		u8 sta_auth = wl->conf.conn.sta_sleep_auth;
		/* Configure for power according to debugfs */
		if (sta_auth != WL1271_PSM_ILLEGAL)
			wl1271_acx_sleep_auth(wl, sta_auth);
		/* Configure for ELP power saving */
		else
			wl1271_acx_sleep_auth(wl, WL1271_PSM_ELP);
	}

unlock:
	mutex_unlock(&wl->mutex);

	del_timer_sync(&wlvif->rx_streaming_timer);
	cancel_work_sync(&wlvif->rx_streaming_enable_work);
	cancel_work_sync(&wlvif->rx_streaming_disable_work);
	cancel_work_sync(&wlvif->rc_update_work);
	cancel_delayed_work_sync(&wlvif->connection_loss_work);
	cancel_delayed_work_sync(&wlvif->channel_switch_work);
	cancel_delayed_work_sync(&wlvif->pending_auth_complete_work);

	mutex_lock(&wl->mutex);
}

static void wl1271_op_remove_interface(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	struct wl12xx_vif *iter;
	struct vif_counter_data vif_count;

	wl12xx_get_vif_count(hw, vif, &vif_count);
	mutex_lock(&wl->mutex);

	if (wl->state == WLCORE_STATE_OFF ||
	    !test_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags))
		goto out;

	/*
	 * wl->vif can be null here if someone shuts down the interface
	 * just when hardware recovery has been started.
	 */
	wl12xx_for_each_wlvif(wl, iter) {
		if (iter != wlvif)
			continue;

		__wl1271_op_remove_interface(wl, vif, true);
		break;
	}
	WARN_ON(iter != wlvif);
	if (wl12xx_need_fw_change(wl, vif_count, false)) {
		wl12xx_force_active_psm(wl);
		set_bit(WL1271_FLAG_INTENDED_FW_RECOVERY, &wl->flags);
		wl12xx_queue_recovery_work(wl);
	}
out:
	mutex_unlock(&wl->mutex);
}

static int wl12xx_op_change_interface(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      enum nl80211_iftype new_type, bool p2p)
{
	struct wl1271 *wl = hw->priv;
	int ret;

	set_bit(WL1271_FLAG_VIF_CHANGE_IN_PROGRESS, &wl->flags);
	wl1271_op_remove_interface(hw, vif);

	vif->type = new_type;
	vif->p2p = p2p;
	ret = wl1271_op_add_interface(hw, vif);

	clear_bit(WL1271_FLAG_VIF_CHANGE_IN_PROGRESS, &wl->flags);
	return ret;
}

static int wlcore_join(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	int ret;
	bool is_ibss = (wlvif->bss_type == BSS_TYPE_IBSS);

	/*
	 * One of the side effects of the JOIN command is that is clears
	 * WPA/WPA2 keys from the chipset. Performing a JOIN while associated
	 * to a WPA/WPA2 access point will therefore kill the data-path.
	 * Currently the only valid scenario for JOIN during association
	 * is on roaming, in which case we will also be given new keys.
	 * Keep the below message for now, unless it starts bothering
	 * users who really like to roam a lot :)
	 */
	if (test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
		wl1271_info("JOIN while associated.");

	/* clear encryption type */
	wlvif->encryption_type = KEY_NONE;

	if (is_ibss)
		ret = wl12xx_cmd_role_start_ibss(wl, wlvif);
	else {
		if (wl->quirks & WLCORE_QUIRK_START_STA_FAILS) {
			/*
			 * TODO: this is an ugly workaround for wl12xx fw
			 * bug - we are not able to tx/rx after the first
			 * start_sta, so make dummy start+stop calls,
			 * and then call start_sta again.
			 * this should be fixed in the fw.
			 */
			wl12xx_cmd_role_start_sta(wl, wlvif);
			wl12xx_cmd_role_stop_sta(wl, wlvif);
		}

		ret = wl12xx_cmd_role_start_sta(wl, wlvif);
	}

	return ret;
}

static int wl1271_ssid_set(struct wl12xx_vif *wlvif, struct sk_buff *skb,
			    int offset)
{
	u8 ssid_len;
	const u8 *ptr = cfg80211_find_ie(WLAN_EID_SSID, skb->data + offset,
					 skb->len - offset);

	if (!ptr) {
		wl1271_error("No SSID in IEs!");
		return -ENOENT;
	}

	ssid_len = ptr[1];
	if (ssid_len > IEEE80211_MAX_SSID_LEN) {
		wl1271_error("SSID is too long!");
		return -EINVAL;
	}

	wlvif->ssid_len = ssid_len;
	memcpy(wlvif->ssid, ptr+2, ssid_len);
	return 0;
}

static int wlcore_set_ssid(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);
	struct sk_buff *skb;
	int ieoffset;

	/* we currently only support setting the ssid from the ap probe req */
	if (wlvif->bss_type != BSS_TYPE_STA_BSS)
		return -EINVAL;

	skb = ieee80211_ap_probereq_get(wl->hw, vif);
	if (!skb)
		return -EINVAL;

	ieoffset = offsetof(struct ieee80211_mgmt,
			    u.probe_req.variable);
	wl1271_ssid_set(wlvif, skb, ieoffset);
	dev_kfree_skb(skb);

	return 0;
}

static int wlcore_set_assoc(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			    struct ieee80211_bss_conf *bss_conf,
			    u32 sta_rate_set)
{
	int ieoffset;
	int ret;

	wlvif->aid = bss_conf->aid;
	wlvif->channel_type = cfg80211_get_chandef_type(&bss_conf->chandef);
	wlvif->beacon_int = bss_conf->beacon_int;
	wlvif->wmm_enabled = bss_conf->qos;

	set_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags);

	/*
	 * with wl1271, we don't need to update the
	 * beacon_int and dtim_period, because the firmware
	 * updates it by itself when the first beacon is
	 * received after a join.
	 */
	ret = wl1271_cmd_build_ps_poll(wl, wlvif, wlvif->aid);
	if (ret < 0)
		return ret;

	/*
	 * Get a template for hardware connection maintenance
	 */
	dev_kfree_skb(wlvif->probereq);
	wlvif->probereq = wl1271_cmd_build_ap_probe_req(wl,
							wlvif,
							NULL);
	ieoffset = offsetof(struct ieee80211_mgmt,
			    u.probe_req.variable);
	wl1271_ssid_set(wlvif, wlvif->probereq, ieoffset);

	/* enable the connection monitoring feature */
	ret = wl1271_acx_conn_monit_params(wl, wlvif, true);
	if (ret < 0)
		return ret;

	/*
	 * The join command disable the keep-alive mode, shut down its process,
	 * and also clear the template config, so we need to reset it all after
	 * the join. The acx_aid starts the keep-alive process, and the order
	 * of the commands below is relevant.
	 */
	ret = wl1271_acx_keep_alive_mode(wl, wlvif, true);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_aid(wl, wlvif, wlvif->aid);
	if (ret < 0)
		return ret;

	ret = wl12xx_cmd_build_klv_null_data(wl, wlvif);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_keep_alive_config(wl, wlvif,
					   wlvif->sta.klv_template_id,
					   ACX_KEEP_ALIVE_TPL_VALID);
	if (ret < 0)
		return ret;

	/*
	 * The default fw psm configuration is AUTO, while mac80211 default
	 * setting is off (ACTIVE), so sync the fw with the correct value.
	 */
	ret = wl1271_ps_set_mode(wl, wlvif, STATION_ACTIVE_MODE);
	if (ret < 0)
		return ret;

	if (sta_rate_set) {
		wlvif->rate_set =
			wl1271_tx_enabled_rates_get(wl,
						    sta_rate_set,
						    wlvif->band);
		ret = wl1271_acx_sta_rate_policies(wl, wlvif);
		if (ret < 0)
			return ret;
	}

	return ret;
}

static int wlcore_unset_assoc(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	int ret;
	bool sta = wlvif->bss_type == BSS_TYPE_STA_BSS;

	/* make sure we are connected (sta) joined */
	if (sta &&
	    !test_and_clear_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
		return false;

	/* make sure we are joined (ibss) */
	if (!sta &&
	    test_and_clear_bit(WLVIF_FLAG_IBSS_JOINED, &wlvif->flags))
		return false;

	if (sta) {
		/* use defaults when not associated */
		wlvif->aid = 0;

		/* free probe-request template */
		dev_kfree_skb(wlvif->probereq);
		wlvif->probereq = NULL;

		/* disable connection monitor features */
		ret = wl1271_acx_conn_monit_params(wl, wlvif, false);
		if (ret < 0)
			return ret;

		/* Disable the keep-alive feature */
		ret = wl1271_acx_keep_alive_mode(wl, wlvif, false);
		if (ret < 0)
			return ret;

		/* disable beacon filtering */
		ret = wl1271_acx_beacon_filter_opt(wl, wlvif, false);
		if (ret < 0)
			return ret;
	}

	if (test_and_clear_bit(WLVIF_FLAG_CS_PROGRESS, &wlvif->flags)) {
		struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);

		wl12xx_cmd_stop_channel_switch(wl, wlvif);
		ieee80211_chswitch_done(vif, false);
		cancel_delayed_work(&wlvif->channel_switch_work);
	}

	/* invalidate keep-alive template */
	wl1271_acx_keep_alive_config(wl, wlvif,
				     wlvif->sta.klv_template_id,
				     ACX_KEEP_ALIVE_TPL_INVALID);

	return 0;
}

static void wl1271_set_band_rate(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	wlvif->basic_rate_set = wlvif->bitrate_masks[wlvif->band];
	wlvif->rate_set = wlvif->basic_rate_set;
}

static void wl1271_sta_handle_idle(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				   bool idle)
{
	bool cur_idle = !test_bit(WLVIF_FLAG_ACTIVE, &wlvif->flags);

	if (idle == cur_idle)
		return;

	if (idle) {
		clear_bit(WLVIF_FLAG_ACTIVE, &wlvif->flags);
	} else {
		/* The current firmware only supports sched_scan in idle */
		if (wl->sched_vif == wlvif)
			wl->ops->sched_scan_stop(wl, wlvif);

		set_bit(WLVIF_FLAG_ACTIVE, &wlvif->flags);
	}
}

static int wl12xx_config_vif(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			     struct ieee80211_conf *conf, u32 changed)
{
	int ret;

	if (wlcore_is_p2p_mgmt(wlvif))
		return 0;

	if (conf->power_level != wlvif->power_level) {
		ret = wl1271_acx_tx_power(wl, wlvif, conf->power_level);
		if (ret < 0)
			return ret;

		wlvif->power_level = conf->power_level;
	}

	return 0;
}

static int wl1271_op_config(struct ieee80211_hw *hw, u32 changed)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif;
	struct ieee80211_conf *conf = &hw->conf;
	int ret = 0;

	wl1271_debug(DEBUG_MAC80211, "mac80211 config psm %s power %d %s"
		     " changed 0x%x",
		     conf->flags & IEEE80211_CONF_PS ? "on" : "off",
		     conf->power_level,
		     conf->flags & IEEE80211_CONF_IDLE ? "idle" : "in use",
			 changed);

	mutex_lock(&wl->mutex);

	if (changed & IEEE80211_CONF_CHANGE_POWER)
		wl->power_level = conf->power_level;

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	/* configure each interface */
	wl12xx_for_each_wlvif(wl, wlvif) {
		ret = wl12xx_config_vif(wl, wlvif, conf, changed);
		if (ret < 0)
			goto out_sleep;
	}

out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

struct wl1271_filter_params {
	bool enabled;
	int mc_list_length;
	u8 mc_list[ACX_MC_ADDRESS_GROUP_MAX][ETH_ALEN];
};

static u64 wl1271_op_prepare_multicast(struct ieee80211_hw *hw,
				       struct netdev_hw_addr_list *mc_list)
{
	struct wl1271_filter_params *fp;
	struct netdev_hw_addr *ha;

	fp = kzalloc(sizeof(*fp), GFP_ATOMIC);
	if (!fp) {
		wl1271_error("Out of memory setting filters.");
		return 0;
	}

	/* update multicast filtering parameters */
	fp->mc_list_length = 0;
	if (netdev_hw_addr_list_count(mc_list) > ACX_MC_ADDRESS_GROUP_MAX) {
		fp->enabled = false;
	} else {
		fp->enabled = true;
		netdev_hw_addr_list_for_each(ha, mc_list) {
			memcpy(fp->mc_list[fp->mc_list_length],
					ha->addr, ETH_ALEN);
			fp->mc_list_length++;
		}
	}

	return (u64)(unsigned long)fp;
}

#define WL1271_SUPPORTED_FILTERS (FIF_ALLMULTI | \
				  FIF_FCSFAIL | \
				  FIF_BCN_PRBRESP_PROMISC | \
				  FIF_CONTROL | \
				  FIF_OTHER_BSS)

static void wl1271_op_configure_filter(struct ieee80211_hw *hw,
				       unsigned int changed,
				       unsigned int *total, u64 multicast)
{
	struct wl1271_filter_params *fp = (void *)(unsigned long)multicast;
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif;

	int ret;

	wl1271_debug(DEBUG_MAC80211, "mac80211 configure filter changed %x"
		     " total %x", changed, *total);

	mutex_lock(&wl->mutex);

	*total &= WL1271_SUPPORTED_FILTERS;
	changed &= WL1271_SUPPORTED_FILTERS;

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	wl12xx_for_each_wlvif(wl, wlvif) {
		if (wlcore_is_p2p_mgmt(wlvif))
			continue;

		if (wlvif->bss_type != BSS_TYPE_AP_BSS) {
			if (*total & FIF_ALLMULTI)
				ret = wl1271_acx_group_address_tbl(wl, wlvif,
								   false,
								   NULL, 0);
			else if (fp)
				ret = wl1271_acx_group_address_tbl(wl, wlvif,
							fp->enabled,
							fp->mc_list,
							fp->mc_list_length);
			if (ret < 0)
				goto out_sleep;
		}

		/*
		 * If interface in AP mode and created with allmulticast then disable
		 * the firmware filters so that all multicast packets are passed
		 * This is mandatory for MDNS based discovery protocols 
		 */
 		if (wlvif->bss_type == BSS_TYPE_AP_BSS) {
 			if (*total & FIF_ALLMULTI) {
				ret = wl1271_acx_group_address_tbl(wl, wlvif,
							false,
							NULL, 0);
				if (ret < 0)
					goto out_sleep;
			}
		}
	}

	/*
	 * the fw doesn't provide an api to configure the filters. instead,
	 * the filters configuration is based on the active roles / ROC
	 * state.
	 */

out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

out:
	mutex_unlock(&wl->mutex);
	kfree(fp);
}

static int wl1271_record_ap_key(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				u8 id, u8 key_type, u8 key_size,
				const u8 *key, u8 hlid, u32 tx_seq_32,
				u16 tx_seq_16)
{
	struct wl1271_ap_key *ap_key;
	int i;

	wl1271_debug(DEBUG_CRYPT, "record ap key id %d", (int)id);

	if (key_size > MAX_KEY_SIZE)
		return -EINVAL;

	/*
	 * Find next free entry in ap_keys. Also check we are not replacing
	 * an existing key.
	 */
	for (i = 0; i < MAX_NUM_KEYS; i++) {
		if (wlvif->ap.recorded_keys[i] == NULL)
			break;

		if (wlvif->ap.recorded_keys[i]->id == id) {
			wl1271_warning("trying to record key replacement");
			return -EINVAL;
		}
	}

	if (i == MAX_NUM_KEYS)
		return -EBUSY;

	ap_key = kzalloc(sizeof(*ap_key), GFP_KERNEL);
	if (!ap_key)
		return -ENOMEM;

	ap_key->id = id;
	ap_key->key_type = key_type;
	ap_key->key_size = key_size;
	memcpy(ap_key->key, key, key_size);
	ap_key->hlid = hlid;
	ap_key->tx_seq_32 = tx_seq_32;
	ap_key->tx_seq_16 = tx_seq_16;

	wlvif->ap.recorded_keys[i] = ap_key;
	return 0;
}

static void wl1271_free_ap_keys(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	int i;

	for (i = 0; i < MAX_NUM_KEYS; i++) {
		kfree(wlvif->ap.recorded_keys[i]);
		wlvif->ap.recorded_keys[i] = NULL;
	}
}

static int wl1271_ap_init_hwenc(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	int i, ret = 0;
	struct wl1271_ap_key *key;
	bool wep_key_added = false;

	for (i = 0; i < MAX_NUM_KEYS; i++) {
		u8 hlid;
		if (wlvif->ap.recorded_keys[i] == NULL)
			break;

		key = wlvif->ap.recorded_keys[i];
		hlid = key->hlid;
		if (hlid == WL12XX_INVALID_LINK_ID)
			hlid = wlvif->ap.bcast_hlid;

		ret = wl1271_cmd_set_ap_key(wl, wlvif, KEY_ADD_OR_REPLACE,
					    key->id, key->key_type,
					    key->key_size, key->key,
					    hlid, key->tx_seq_32,
					    key->tx_seq_16);
		if (ret < 0)
			goto out;

		if (key->key_type == KEY_WEP)
			wep_key_added = true;
	}

	if (wep_key_added) {
		ret = wl12xx_cmd_set_default_wep_key(wl, wlvif->default_key,
						     wlvif->ap.bcast_hlid);
		if (ret < 0)
			goto out;
	}

out:
	wl1271_free_ap_keys(wl, wlvif);
	return ret;
}

static int wl1271_set_key(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		       u16 action, u8 id, u8 key_type,
		       u8 key_size, const u8 *key, u32 tx_seq_32,
		       u16 tx_seq_16, struct ieee80211_sta *sta)
{
	int ret;
	bool is_ap = (wlvif->bss_type == BSS_TYPE_AP_BSS);

	if (is_ap) {
		struct wl1271_station *wl_sta;
		u8 hlid;

		if (sta) {
			wl_sta = (struct wl1271_station *)sta->drv_priv;
			hlid = wl_sta->hlid;
		} else {
			hlid = wlvif->ap.bcast_hlid;
		}

		if (!test_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags)) {
			/*
			 * We do not support removing keys after AP shutdown.
			 * Pretend we do to make mac80211 happy.
			 */
			if (action != KEY_ADD_OR_REPLACE)
				return 0;

			ret = wl1271_record_ap_key(wl, wlvif, id,
					     key_type, key_size,
					     key, hlid, tx_seq_32,
					     tx_seq_16);
		} else {
			ret = wl1271_cmd_set_ap_key(wl, wlvif, action,
					     id, key_type, key_size,
					     key, hlid, tx_seq_32,
					     tx_seq_16);
		}

		if (ret < 0)
			return ret;
	} else {
		const u8 *addr;
		static const u8 bcast_addr[ETH_ALEN] = {
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff
		};

		addr = sta ? sta->addr : bcast_addr;

		if (is_zero_ether_addr(addr)) {
			/* We dont support TX only encryption */
			return -EOPNOTSUPP;
		}

		/* The wl1271 does not allow to remove unicast keys - they
		   will be cleared automatically on next CMD_JOIN. Ignore the
		   request silently, as we dont want the mac80211 to emit
		   an error message. */
		if (action == KEY_REMOVE && !is_broadcast_ether_addr(addr))
			return 0;

		/* don't remove key if hlid was already deleted */
		if (action == KEY_REMOVE &&
		    wlvif->sta.hlid == WL12XX_INVALID_LINK_ID)
			return 0;

		ret = wl1271_cmd_set_sta_key(wl, wlvif, action,
					     id, key_type, key_size,
					     key, addr, tx_seq_32,
					     tx_seq_16);
		if (ret < 0)
			return ret;

	}

	return 0;
}

static int wlcore_op_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct ieee80211_key_conf *key_conf)
{
	struct wl1271 *wl = hw->priv;
	int ret;
	bool might_change_spare =
		key_conf->cipher == WL1271_CIPHER_SUITE_GEM ||
		key_conf->cipher == WLAN_CIPHER_SUITE_TKIP;

	if (might_change_spare) {
		/*
		 * stop the queues and flush to ensure the next packets are
		 * in sync with FW spare block accounting
		 */
		wlcore_stop_queues(wl, WLCORE_QUEUE_STOP_REASON_SPARE_BLK);
		wl1271_tx_flush(wl);
	}

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		ret = -EAGAIN;
		goto out_wake_queues;
	}

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out_wake_queues;
	}

	ret = wlcore_hw_set_key(wl, cmd, vif, sta, key_conf);

	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

out_wake_queues:
	if (might_change_spare)
		wlcore_wake_queues(wl, WLCORE_QUEUE_STOP_REASON_SPARE_BLK);

	mutex_unlock(&wl->mutex);

	return ret;
}

int wlcore_set_key(struct wl1271 *wl, enum set_key_cmd cmd,
		   struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta,
		   struct ieee80211_key_conf *key_conf)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	int ret;
	u32 tx_seq_32 = 0;
	u16 tx_seq_16 = 0;
	u8 key_type;
	u8 hlid;

	wl1271_debug(DEBUG_MAC80211, "mac80211 set key");

	wl1271_debug(DEBUG_CRYPT, "CMD: 0x%x sta: %p", cmd, sta);
	wl1271_debug(DEBUG_CRYPT, "Key: algo:0x%x, id:%d, len:%d flags 0x%x",
		     key_conf->cipher, key_conf->keyidx,
		     key_conf->keylen, key_conf->flags);
	wl1271_dump(DEBUG_CRYPT, "KEY: ", key_conf->key, key_conf->keylen);

	if (wlvif->bss_type == BSS_TYPE_AP_BSS)
		if (sta) {
			struct wl1271_station *wl_sta = (void *)sta->drv_priv;
			hlid = wl_sta->hlid;
		} else {
			hlid = wlvif->ap.bcast_hlid;
		}
	else
		hlid = wlvif->sta.hlid;

	if (hlid != WL12XX_INVALID_LINK_ID) {
		u64 tx_seq = wl->links[hlid].total_freed_pkts;
		tx_seq_32 = WL1271_TX_SECURITY_HI32(tx_seq);
		tx_seq_16 = WL1271_TX_SECURITY_LO16(tx_seq);
	}

	switch (key_conf->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		key_type = KEY_WEP;

		key_conf->hw_key_idx = key_conf->keyidx;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		key_type = KEY_TKIP;
		key_conf->hw_key_idx = key_conf->keyidx;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key_type = KEY_AES;
		key_conf->flags |= IEEE80211_KEY_FLAG_PUT_IV_SPACE;
		break;
	case WL1271_CIPHER_SUITE_GEM:
		key_type = KEY_GEM;
		break;
	default:
		wl1271_error("Unknown key algo 0x%x", key_conf->cipher);

		return -EOPNOTSUPP;
	}

	switch (cmd) {
	case SET_KEY:
		ret = wl1271_set_key(wl, wlvif, KEY_ADD_OR_REPLACE,
				 key_conf->keyidx, key_type,
				 key_conf->keylen, key_conf->key,
				 tx_seq_32, tx_seq_16, sta);
		if (ret < 0) {
			wl1271_error("Could not add or replace key");
			return ret;
		}

		/*
		 * reconfiguring arp response if the unicast (or common)
		 * encryption key type was changed
		 */
		if (wlvif->bss_type == BSS_TYPE_STA_BSS &&
		    (sta || key_type == KEY_WEP) &&
		    wlvif->encryption_type != key_type) {
			wlvif->encryption_type = key_type;
			ret = wl1271_cmd_build_arp_rsp(wl, wlvif);
			if (ret < 0) {
				wl1271_warning("build arp rsp failed: %d", ret);
				return ret;
			}
		}
		break;

	case DISABLE_KEY:
		ret = wl1271_set_key(wl, wlvif, KEY_REMOVE,
				     key_conf->keyidx, key_type,
				     key_conf->keylen, key_conf->key,
				     0, 0, sta);
		if (ret < 0) {
			wl1271_error("Could not remove key");
			return ret;
		}
		break;

	default:
		wl1271_error("Unsupported key cmd 0x%x", cmd);
		return -EOPNOTSUPP;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(wlcore_set_key);

static void wl1271_op_set_default_key_idx(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  int key_idx)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	int ret;

	wl1271_debug(DEBUG_MAC80211, "mac80211 set default key idx %d",
		     key_idx);

	/* we don't handle unsetting of default key */
	if (key_idx == -1)
		return;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		ret = -EAGAIN;
		goto out_unlock;
	}

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out_unlock;
	}

	wlvif->default_key = key_idx;

	/* the default WEP key needs to be configured at least once */
	if (wlvif->encryption_type == KEY_WEP) {
		ret = wl12xx_cmd_set_default_wep_key(wl,
				key_idx,
				wlvif->sta.hlid);
		if (ret < 0)
			goto out_sleep;
	}

out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

out_unlock:
	mutex_unlock(&wl->mutex);
}

void wlcore_regdomain_config(struct wl1271 *wl)
{
	int ret;

	if (!(wl->quirks & WLCORE_QUIRK_REGDOMAIN_CONF))
		return;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0)
		goto out;

	ret = wlcore_cmd_regdomain_config_locked(wl);
	if (ret < 0) {
		wl12xx_queue_recovery_work(wl);
		goto out;
	}

	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);
}

static int wl1271_op_hw_scan(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_scan_request *hw_req)
{
	struct cfg80211_scan_request *req = &hw_req->req;
	struct wl1271 *wl = hw->priv;
	int ret;
	u8 *ssid = NULL;
	size_t len = 0;

	wl1271_debug(DEBUG_MAC80211, "mac80211 hw scan");

	if (req->n_ssids) {
		ssid = req->ssids[0].ssid;
		len = req->ssids[0].ssid_len;
	}

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		/*
		 * We cannot return -EBUSY here because cfg80211 will expect
		 * a call to ieee80211_scan_completed if we do - in this case
		 * there won't be any call.
		 */
		ret = -EAGAIN;
		goto out;
	}

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	/* fail if there is any role in ROC */
	if (find_first_bit(wl->roc_map, WL12XX_MAX_ROLES) < WL12XX_MAX_ROLES) {
		/* don't allow scanning right now */
		ret = -EBUSY;
		goto out_sleep;
	}

	ret = wlcore_scan(hw->priv, vif, ssid, len, req);
out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static void wl1271_op_cancel_hw_scan(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	struct cfg80211_scan_info info = {
		.aborted = true,
	};
	int ret;

	wl1271_debug(DEBUG_MAC80211, "mac80211 cancel hw scan");

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	if (wl->scan.state == WL1271_SCAN_STATE_IDLE)
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	if (wl->scan.state != WL1271_SCAN_STATE_DONE) {
		ret = wl->ops->scan_stop(wl, wlvif);
		if (ret < 0)
			goto out_sleep;
	}

	/*
	 * Rearm the tx watchdog just before idling scan. This
	 * prevents just-finished scans from triggering the watchdog
	 */
	wl12xx_rearm_tx_watchdog_locked(wl);

	wl->scan.state = WL1271_SCAN_STATE_IDLE;
	memset(wl->scan.scanned_ch, 0, sizeof(wl->scan.scanned_ch));
	wl->scan_wlvif = NULL;
	wl->scan.req = NULL;
	ieee80211_scan_completed(wl->hw, &info);

out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);

	cancel_delayed_work_sync(&wl->scan_complete_work);
}

static int wl1271_op_sched_scan_start(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct cfg80211_sched_scan_request *req,
				      struct ieee80211_scan_ies *ies)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	int ret;

	wl1271_debug(DEBUG_MAC80211, "wl1271_op_sched_scan_start");

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	ret = wl->ops->sched_scan_start(wl, wlvif, req, ies);
	if (ret < 0)
		goto out_sleep;

	wl->sched_vif = wlvif;

out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);
	return ret;
}

static int wl1271_op_sched_scan_stop(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	int ret;

	wl1271_debug(DEBUG_MAC80211, "wl1271_op_sched_scan_stop");

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	wl->ops->sched_scan_stop(wl, wlvif);

	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);

	return 0;
}

static int wl1271_op_set_frag_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct wl1271 *wl = hw->priv;
	int ret = 0;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	ret = wl1271_acx_frag_threshold(wl, value);
	if (ret < 0)
		wl1271_warning("wl1271_op_set_frag_threshold failed: %d", ret);

	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static int wl1271_op_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif;
	int ret = 0;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	wl12xx_for_each_wlvif(wl, wlvif) {
		ret = wl1271_acx_rts_threshold(wl, wlvif, value);
		if (ret < 0)
			wl1271_warning("set rts threshold failed: %d", ret);
	}
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static void wl12xx_remove_ie(struct sk_buff *skb, u8 eid, int ieoffset)
{
	int len;
	const u8 *next, *end = skb->data + skb->len;
	u8 *ie = (u8 *)cfg80211_find_ie(eid, skb->data + ieoffset,
					skb->len - ieoffset);
	if (!ie)
		return;
	len = ie[1] + 2;
	next = ie + len;
	memmove(ie, next, end - next);
	skb_trim(skb, skb->len - len);
}

static void wl12xx_remove_vendor_ie(struct sk_buff *skb,
					    unsigned int oui, u8 oui_type,
					    int ieoffset)
{
	int len;
	const u8 *next, *end = skb->data + skb->len;
	u8 *ie = (u8 *)cfg80211_find_vendor_ie(oui, oui_type,
					       skb->data + ieoffset,
					       skb->len - ieoffset);
	if (!ie)
		return;
	len = ie[1] + 2;
	next = ie + len;
	memmove(ie, next, end - next);
	skb_trim(skb, skb->len - len);
}

static int wl1271_ap_set_probe_resp_tmpl(struct wl1271 *wl, u32 rates,
					 struct ieee80211_vif *vif)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	struct sk_buff *skb;
	int ret;

	skb = ieee80211_proberesp_get(wl->hw, vif);
	if (!skb)
		return -EOPNOTSUPP;

	ret = wl1271_cmd_template_set(wl, wlvif->role_id,
				      CMD_TEMPL_AP_PROBE_RESPONSE,
				      skb->data,
				      skb->len, 0,
				      rates);
	dev_kfree_skb(skb);

	if (ret < 0)
		goto out;

	wl1271_debug(DEBUG_AP, "probe response updated");
	set_bit(WLVIF_FLAG_AP_PROBE_RESP_SET, &wlvif->flags);

out:
	return ret;
}

static int wl1271_ap_set_probe_resp_tmpl_legacy(struct wl1271 *wl,
					     struct ieee80211_vif *vif,
					     u8 *probe_rsp_data,
					     size_t probe_rsp_len,
					     u32 rates)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;
	u8 probe_rsp_templ[WL1271_CMD_TEMPL_MAX_SIZE];
	int ssid_ie_offset, ie_offset, templ_len;
	const u8 *ptr;

	/* no need to change probe response if the SSID is set correctly */
	if (wlvif->ssid_len > 0)
		return wl1271_cmd_template_set(wl, wlvif->role_id,
					       CMD_TEMPL_AP_PROBE_RESPONSE,
					       probe_rsp_data,
					       probe_rsp_len, 0,
					       rates);

	if (probe_rsp_len + bss_conf->ssid_len > WL1271_CMD_TEMPL_MAX_SIZE) {
		wl1271_error("probe_rsp template too big");
		return -EINVAL;
	}

	/* start searching from IE offset */
	ie_offset = offsetof(struct ieee80211_mgmt, u.probe_resp.variable);

	ptr = cfg80211_find_ie(WLAN_EID_SSID, probe_rsp_data + ie_offset,
			       probe_rsp_len - ie_offset);
	if (!ptr) {
		wl1271_error("No SSID in beacon!");
		return -EINVAL;
	}

	ssid_ie_offset = ptr - probe_rsp_data;
	ptr += (ptr[1] + 2);

	memcpy(probe_rsp_templ, probe_rsp_data, ssid_ie_offset);

	/* insert SSID from bss_conf */
	probe_rsp_templ[ssid_ie_offset] = WLAN_EID_SSID;
	probe_rsp_templ[ssid_ie_offset + 1] = bss_conf->ssid_len;
	memcpy(probe_rsp_templ + ssid_ie_offset + 2,
	       bss_conf->ssid, bss_conf->ssid_len);
	templ_len = ssid_ie_offset + 2 + bss_conf->ssid_len;

	memcpy(probe_rsp_templ + ssid_ie_offset + 2 + bss_conf->ssid_len,
	       ptr, probe_rsp_len - (ptr - probe_rsp_data));
	templ_len += probe_rsp_len - (ptr - probe_rsp_data);

	return wl1271_cmd_template_set(wl, wlvif->role_id,
				       CMD_TEMPL_AP_PROBE_RESPONSE,
				       probe_rsp_templ,
				       templ_len, 0,
				       rates);
}

static int wl1271_bss_erp_info_changed(struct wl1271 *wl,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *bss_conf,
				       u32 changed)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	int ret = 0;

	if (changed & BSS_CHANGED_ERP_SLOT) {
		if (bss_conf->use_short_slot)
			ret = wl1271_acx_slot(wl, wlvif, SLOT_TIME_SHORT);
		else
			ret = wl1271_acx_slot(wl, wlvif, SLOT_TIME_LONG);
		if (ret < 0) {
			wl1271_warning("Set slot time failed %d", ret);
			goto out;
		}
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		if (bss_conf->use_short_preamble)
			wl1271_acx_set_preamble(wl, wlvif, ACX_PREAMBLE_SHORT);
		else
			wl1271_acx_set_preamble(wl, wlvif, ACX_PREAMBLE_LONG);
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		if (bss_conf->use_cts_prot)
			ret = wl1271_acx_cts_protect(wl, wlvif,
						     CTSPROTECT_ENABLE);
		else
			ret = wl1271_acx_cts_protect(wl, wlvif,
						     CTSPROTECT_DISABLE);
		if (ret < 0) {
			wl1271_warning("Set ctsprotect failed %d", ret);
			goto out;
		}
	}

out:
	return ret;
}

static int wlcore_set_beacon_template(struct wl1271 *wl,
				      struct ieee80211_vif *vif,
				      bool is_ap)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	struct ieee80211_hdr *hdr;
	u32 min_rate;
	int ret;
	int ieoffset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	struct sk_buff *beacon = ieee80211_beacon_get(wl->hw, vif);
	u16 tmpl_id;

	if (!beacon) {
		ret = -EINVAL;
		goto out;
	}

	wl1271_debug(DEBUG_MASTER, "beacon updated");

	ret = wl1271_ssid_set(wlvif, beacon, ieoffset);
	if (ret < 0) {
		dev_kfree_skb(beacon);
		goto out;
	}
	min_rate = wl1271_tx_min_rate_get(wl, wlvif->basic_rate_set);
	tmpl_id = is_ap ? CMD_TEMPL_AP_BEACON :
		CMD_TEMPL_BEACON;
	ret = wl1271_cmd_template_set(wl, wlvif->role_id, tmpl_id,
				      beacon->data,
				      beacon->len, 0,
				      min_rate);
	if (ret < 0) {
		dev_kfree_skb(beacon);
		goto out;
	}

	wlvif->wmm_enabled =
		cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
					WLAN_OUI_TYPE_MICROSOFT_WMM,
					beacon->data + ieoffset,
					beacon->len - ieoffset);

	/*
	 * In case we already have a probe-resp beacon set explicitly
	 * by usermode, don't use the beacon data.
	 */
	if (test_bit(WLVIF_FLAG_AP_PROBE_RESP_SET, &wlvif->flags))
		goto end_bcn;

	/* remove TIM ie from probe response */
	wl12xx_remove_ie(beacon, WLAN_EID_TIM, ieoffset);

	/*
	 * remove p2p ie from probe response.
	 * the fw reponds to probe requests that don't include
	 * the p2p ie. probe requests with p2p ie will be passed,
	 * and will be responded by the supplicant (the spec
	 * forbids including the p2p ie when responding to probe
	 * requests that didn't include it).
	 */
	wl12xx_remove_vendor_ie(beacon, WLAN_OUI_WFA,
				WLAN_OUI_TYPE_WFA_P2P, ieoffset);

	hdr = (struct ieee80211_hdr *) beacon->data;
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					 IEEE80211_STYPE_PROBE_RESP);
	if (is_ap)
		ret = wl1271_ap_set_probe_resp_tmpl_legacy(wl, vif,
							   beacon->data,
							   beacon->len,
							   min_rate);
	else
		ret = wl1271_cmd_template_set(wl, wlvif->role_id,
					      CMD_TEMPL_PROBE_RESPONSE,
					      beacon->data,
					      beacon->len, 0,
					      min_rate);
end_bcn:
	dev_kfree_skb(beacon);
	if (ret < 0)
		goto out;

out:
	return ret;
}

static int wl1271_bss_beacon_info_changed(struct wl1271 *wl,
					  struct ieee80211_vif *vif,
					  struct ieee80211_bss_conf *bss_conf,
					  u32 changed)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	bool is_ap = (wlvif->bss_type == BSS_TYPE_AP_BSS);
	int ret = 0;

	if (changed & BSS_CHANGED_BEACON_INT) {
		wl1271_debug(DEBUG_MASTER, "beacon interval updated: %d",
			bss_conf->beacon_int);

		wlvif->beacon_int = bss_conf->beacon_int;
	}

	if ((changed & BSS_CHANGED_AP_PROBE_RESP) && is_ap) {
		u32 rate = wl1271_tx_min_rate_get(wl, wlvif->basic_rate_set);

		wl1271_ap_set_probe_resp_tmpl(wl, rate, vif);
	}

	if (changed & BSS_CHANGED_BEACON) {
		ret = wlcore_set_beacon_template(wl, vif, is_ap);
		if (ret < 0)
			goto out;

		if (test_and_clear_bit(WLVIF_FLAG_BEACON_DISABLED,
				       &wlvif->flags)) {
			ret = wlcore_hw_dfs_master_restart(wl, wlvif);
			if (ret < 0)
				goto out;
		}
	}
out:
	if (ret != 0)
		wl1271_error("beacon info change failed: %d", ret);
	return ret;
}

/* AP mode changes */
static void wl1271_bss_info_changed_ap(struct wl1271 *wl,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *bss_conf,
				       u32 changed)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	int ret = 0;

	if (changed & BSS_CHANGED_BASIC_RATES) {
		u32 rates = bss_conf->basic_rates;

		wlvif->basic_rate_set = wl1271_tx_enabled_rates_get(wl, rates,
								 wlvif->band);
		wlvif->basic_rate = wl1271_tx_min_rate_get(wl,
							wlvif->basic_rate_set);

		ret = wl1271_init_ap_rates(wl, wlvif);
		if (ret < 0) {
			wl1271_error("AP rate policy change failed %d", ret);
			goto out;
		}

		ret = wl1271_ap_init_templates(wl, vif);
		if (ret < 0)
			goto out;

		/* No need to set probe resp template for mesh */
		if (!ieee80211_vif_is_mesh(vif)) {
			ret = wl1271_ap_set_probe_resp_tmpl(wl,
							    wlvif->basic_rate,
							    vif);
			if (ret < 0)
				goto out;
		}

		ret = wlcore_set_beacon_template(wl, vif, true);
		if (ret < 0)
			goto out;
	}

	ret = wl1271_bss_beacon_info_changed(wl, vif, bss_conf, changed);
	if (ret < 0)
		goto out;

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		if (bss_conf->enable_beacon) {
			if (!test_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags)) {
				ret = wl12xx_cmd_role_start_ap(wl, wlvif);
				if (ret < 0)
					goto out;

				ret = wl1271_ap_init_hwenc(wl, wlvif);
				if (ret < 0)
					goto out;

				set_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags);
				wl1271_debug(DEBUG_AP, "started AP");
			}
		} else {
			if (test_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags)) {
				/*
				 * AP might be in ROC in case we have just
				 * sent auth reply. handle it.
				 */
				if (test_bit(wlvif->role_id, wl->roc_map))
					wl12xx_croc(wl, wlvif->role_id);

				ret = wl12xx_cmd_role_stop_ap(wl, wlvif);
				if (ret < 0)
					goto out;

				clear_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags);
				clear_bit(WLVIF_FLAG_AP_PROBE_RESP_SET,
					  &wlvif->flags);
				wl1271_debug(DEBUG_AP, "stopped AP");
			}
		}
	}

	ret = wl1271_bss_erp_info_changed(wl, vif, bss_conf, changed);
	if (ret < 0)
		goto out;

	/* Handle HT information change */
	if ((changed & BSS_CHANGED_HT) &&
	    (bss_conf->chandef.width != NL80211_CHAN_WIDTH_20_NOHT)) {
		ret = wl1271_acx_set_ht_information(wl, wlvif,
					bss_conf->ht_operation_mode);
		if (ret < 0) {
			wl1271_warning("Set ht information failed %d", ret);
			goto out;
		}
	}

out:
	return;
}

static int wlcore_set_bssid(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			    struct ieee80211_bss_conf *bss_conf,
			    u32 sta_rate_set)
{
	u32 rates;
	int ret;

	wl1271_debug(DEBUG_MAC80211,
	     "changed_bssid: %pM, aid: %d, bcn_int: %d, brates: 0x%x sta_rate_set: 0x%x",
	     bss_conf->bssid, bss_conf->aid,
	     bss_conf->beacon_int,
	     bss_conf->basic_rates, sta_rate_set);

	wlvif->beacon_int = bss_conf->beacon_int;
	rates = bss_conf->basic_rates;
	wlvif->basic_rate_set =
		wl1271_tx_enabled_rates_get(wl, rates,
					    wlvif->band);
	wlvif->basic_rate =
		wl1271_tx_min_rate_get(wl,
				       wlvif->basic_rate_set);

	if (sta_rate_set)
		wlvif->rate_set =
			wl1271_tx_enabled_rates_get(wl,
						sta_rate_set,
						wlvif->band);

	/* we only support sched_scan while not connected */
	if (wl->sched_vif == wlvif)
		wl->ops->sched_scan_stop(wl, wlvif);

	ret = wl1271_acx_sta_rate_policies(wl, wlvif);
	if (ret < 0)
		return ret;

	ret = wl12xx_cmd_build_null_data(wl, wlvif);
	if (ret < 0)
		return ret;

	ret = wl1271_build_qos_null_data(wl, wl12xx_wlvif_to_vif(wlvif));
	if (ret < 0)
		return ret;

	wlcore_set_ssid(wl, wlvif);

	set_bit(WLVIF_FLAG_IN_USE, &wlvif->flags);

	return 0;
}

static int wlcore_clear_bssid(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	int ret;

	/* revert back to minimum rates for the current band */
	wl1271_set_band_rate(wl, wlvif);
	wlvif->basic_rate = wl1271_tx_min_rate_get(wl, wlvif->basic_rate_set);

	ret = wl1271_acx_sta_rate_policies(wl, wlvif);
	if (ret < 0)
		return ret;

	if (wlvif->bss_type == BSS_TYPE_STA_BSS &&
	    test_bit(WLVIF_FLAG_IN_USE, &wlvif->flags)) {
		ret = wl12xx_cmd_role_stop_sta(wl, wlvif);
		if (ret < 0)
			return ret;
	}

	clear_bit(WLVIF_FLAG_IN_USE, &wlvif->flags);
	return 0;
}
/* STA/IBSS mode changes */
static void wl1271_bss_info_changed_sta(struct wl1271 *wl,
					struct ieee80211_vif *vif,
					struct ieee80211_bss_conf *bss_conf,
					u32 changed)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	bool do_join = false;
	bool is_ibss = (wlvif->bss_type == BSS_TYPE_IBSS);
	bool ibss_joined = false;
	u32 sta_rate_set = 0;
	int ret;
	struct ieee80211_sta *sta;
	bool sta_exists = false;
	struct ieee80211_sta_ht_cap sta_ht_cap;

	if (is_ibss) {
		ret = wl1271_bss_beacon_info_changed(wl, vif, bss_conf,
						     changed);
		if (ret < 0)
			goto out;
	}

	if (changed & BSS_CHANGED_IBSS) {
		if (bss_conf->ibss_joined) {
			set_bit(WLVIF_FLAG_IBSS_JOINED, &wlvif->flags);
			ibss_joined = true;
		} else {
			wlcore_unset_assoc(wl, wlvif);
			wl12xx_cmd_role_stop_sta(wl, wlvif);
		}
	}

	if ((changed & BSS_CHANGED_BEACON_INT) && ibss_joined)
		do_join = true;

	/* Need to update the SSID (for filtering etc) */
	if ((changed & BSS_CHANGED_BEACON) && ibss_joined)
		do_join = true;

	if ((changed & BSS_CHANGED_BEACON_ENABLED) && ibss_joined) {
		wl1271_debug(DEBUG_ADHOC, "ad-hoc beaconing: %s",
			     bss_conf->enable_beacon ? "enabled" : "disabled");

		do_join = true;
	}

	if (changed & BSS_CHANGED_IDLE && !is_ibss)
		wl1271_sta_handle_idle(wl, wlvif, bss_conf->idle);

	if (changed & BSS_CHANGED_CQM) {
		bool enable = false;
		if (bss_conf->cqm_rssi_thold)
			enable = true;
		ret = wl1271_acx_rssi_snr_trigger(wl, wlvif, enable,
						  bss_conf->cqm_rssi_thold,
						  bss_conf->cqm_rssi_hyst);
		if (ret < 0)
			goto out;
		wlvif->rssi_thold = bss_conf->cqm_rssi_thold;
	}

	if (changed & (BSS_CHANGED_BSSID | BSS_CHANGED_HT |
		       BSS_CHANGED_ASSOC)) {
		rcu_read_lock();
		sta = ieee80211_find_sta(vif, bss_conf->bssid);
		if (sta) {
			u8 *rx_mask = sta->ht_cap.mcs.rx_mask;

			/* save the supp_rates of the ap */
			sta_rate_set = sta->supp_rates[wlvif->band];
			if (sta->ht_cap.ht_supported)
				sta_rate_set |=
					(rx_mask[0] << HW_HT_RATES_OFFSET) |
					(rx_mask[1] << HW_MIMO_RATES_OFFSET);
			sta_ht_cap = sta->ht_cap;
			sta_exists = true;
		}

		rcu_read_unlock();
	}

	if (changed & BSS_CHANGED_BSSID) {
		if (!is_zero_ether_addr(bss_conf->bssid)) {
			ret = wlcore_set_bssid(wl, wlvif, bss_conf,
					       sta_rate_set);
			if (ret < 0)
				goto out;

			/* Need to update the BSSID (for filtering etc) */
			do_join = true;
		} else {
			ret = wlcore_clear_bssid(wl, wlvif);
			if (ret < 0)
				goto out;
		}
	}

	if (changed & BSS_CHANGED_IBSS) {
		wl1271_debug(DEBUG_ADHOC, "ibss_joined: %d",
			     bss_conf->ibss_joined);

		if (bss_conf->ibss_joined) {
			u32 rates = bss_conf->basic_rates;
			wlvif->basic_rate_set =
				wl1271_tx_enabled_rates_get(wl, rates,
							    wlvif->band);
			wlvif->basic_rate =
				wl1271_tx_min_rate_get(wl,
						       wlvif->basic_rate_set);

			/* by default, use 11b + OFDM rates */
			wlvif->rate_set = CONF_TX_IBSS_DEFAULT_RATES;
			ret = wl1271_acx_sta_rate_policies(wl, wlvif);
			if (ret < 0)
				goto out;
		}
	}

	if ((changed & BSS_CHANGED_BEACON_INFO) && bss_conf->dtim_period) {
		/* enable beacon filtering */
		ret = wl1271_acx_beacon_filter_opt(wl, wlvif, true);
		if (ret < 0)
			goto out;
	}

	ret = wl1271_bss_erp_info_changed(wl, vif, bss_conf, changed);
	if (ret < 0)
		goto out;

	if (do_join) {
		ret = wlcore_join(wl, wlvif);
		if (ret < 0) {
			wl1271_warning("cmd join failed %d", ret);
			goto out;
		}
	}

	if (changed & BSS_CHANGED_ASSOC) {
		if (bss_conf->assoc) {
			ret = wlcore_set_assoc(wl, wlvif, bss_conf,
					       sta_rate_set);
			if (ret < 0)
				goto out;

			if (test_bit(WLVIF_FLAG_STA_AUTHORIZED, &wlvif->flags))
				wl12xx_set_authorized(wl, wlvif);
		} else {
			wlcore_unset_assoc(wl, wlvif);
		}
	}

	if (changed & BSS_CHANGED_PS) {
		if ((bss_conf->ps) &&
		    test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags) &&
		    !test_bit(WLVIF_FLAG_IN_PS, &wlvif->flags)) {
			int ps_mode;
			char *ps_mode_str;

			if (wl->conf.conn.forced_ps) {
				ps_mode = STATION_POWER_SAVE_MODE;
				ps_mode_str = "forced";
			} else {
				ps_mode = STATION_AUTO_PS_MODE;
				ps_mode_str = "auto";
			}

			wl1271_debug(DEBUG_PSM, "%s ps enabled", ps_mode_str);

			ret = wl1271_ps_set_mode(wl, wlvif, ps_mode);
			if (ret < 0)
				wl1271_warning("enter %s ps failed %d",
					       ps_mode_str, ret);
		} else if (!bss_conf->ps &&
			   test_bit(WLVIF_FLAG_IN_PS, &wlvif->flags)) {
			wl1271_debug(DEBUG_PSM, "auto ps disabled");

			ret = wl1271_ps_set_mode(wl, wlvif,
						 STATION_ACTIVE_MODE);
			if (ret < 0)
				wl1271_warning("exit auto ps failed %d", ret);
		}
	}

	/* Handle new association with HT. Do this after join. */
	if (sta_exists) {
		bool enabled =
			bss_conf->chandef.width != NL80211_CHAN_WIDTH_20_NOHT;

		ret = wlcore_hw_set_peer_cap(wl,
					     &sta_ht_cap,
					     enabled,
					     wlvif->rate_set,
					     wlvif->sta.hlid);
		if (ret < 0) {
			wl1271_warning("Set ht cap failed %d", ret);
			goto out;

		}

		if (enabled) {
			ret = wl1271_acx_set_ht_information(wl, wlvif,
						bss_conf->ht_operation_mode);
			if (ret < 0) {
				wl1271_warning("Set ht information failed %d",
					       ret);
				goto out;
			}
		}
	}

	/* Handle arp filtering. Done after join. */
	if ((changed & BSS_CHANGED_ARP_FILTER) ||
	    (!is_ibss && (changed & BSS_CHANGED_QOS))) {
		__be32 addr = bss_conf->arp_addr_list[0];
		wlvif->sta.qos = bss_conf->qos;
		WARN_ON(wlvif->bss_type != BSS_TYPE_STA_BSS);

		if (bss_conf->arp_addr_cnt == 1 && bss_conf->assoc) {
			wlvif->ip_addr = addr;
			/*
			 * The template should have been configured only upon
			 * association. however, it seems that the correct ip
			 * isn't being set (when sending), so we have to
			 * reconfigure the template upon every ip change.
			 */
			ret = wl1271_cmd_build_arp_rsp(wl, wlvif);
			if (ret < 0) {
				wl1271_warning("build arp rsp failed: %d", ret);
				goto out;
			}

			ret = wl1271_acx_arp_ip_filter(wl, wlvif,
				(ACX_ARP_FILTER_ARP_FILTERING |
				 ACX_ARP_FILTER_AUTO_ARP),
				addr);
		} else {
			wlvif->ip_addr = 0;
			ret = wl1271_acx_arp_ip_filter(wl, wlvif, 0, addr);
		}

		if (ret < 0)
			goto out;
	}

out:
	return;
}

static void wl1271_op_bss_info_changed(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *bss_conf,
				       u32 changed)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	bool is_ap = (wlvif->bss_type == BSS_TYPE_AP_BSS);
	int ret;

	wl1271_debug(DEBUG_MAC80211, "mac80211 bss info role %d changed 0x%x",
		     wlvif->role_id, (int)changed);

	/*
	 * make sure to cancel pending disconnections if our association
	 * state changed
	 */
	if (!is_ap && (changed & BSS_CHANGED_ASSOC))
		cancel_delayed_work_sync(&wlvif->connection_loss_work);

	if (is_ap && (changed & BSS_CHANGED_BEACON_ENABLED) &&
	    !bss_conf->enable_beacon)
		wl1271_tx_flush(wl);

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	if (unlikely(!test_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags)))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	if ((changed & BSS_CHANGED_TXPOWER) &&
	    bss_conf->txpower != wlvif->power_level) {

		ret = wl1271_acx_tx_power(wl, wlvif, bss_conf->txpower);
		if (ret < 0)
			goto out;

		wlvif->power_level = bss_conf->txpower;
	}

	if (is_ap)
		wl1271_bss_info_changed_ap(wl, vif, bss_conf, changed);
	else
		wl1271_bss_info_changed_sta(wl, vif, bss_conf, changed);

	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

out:
	mutex_unlock(&wl->mutex);
}

static int wlcore_op_add_chanctx(struct ieee80211_hw *hw,
				 struct ieee80211_chanctx_conf *ctx)
{
	wl1271_debug(DEBUG_MAC80211, "mac80211 add chanctx %d (type %d)",
		     ieee80211_frequency_to_channel(ctx->def.chan->center_freq),
		     cfg80211_get_chandef_type(&ctx->def));
	return 0;
}

static void wlcore_op_remove_chanctx(struct ieee80211_hw *hw,
				     struct ieee80211_chanctx_conf *ctx)
{
	wl1271_debug(DEBUG_MAC80211, "mac80211 remove chanctx %d (type %d)",
		     ieee80211_frequency_to_channel(ctx->def.chan->center_freq),
		     cfg80211_get_chandef_type(&ctx->def));
}

static void wlcore_op_change_chanctx(struct ieee80211_hw *hw,
				     struct ieee80211_chanctx_conf *ctx,
				     u32 changed)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif;
	int ret;
	int channel = ieee80211_frequency_to_channel(
		ctx->def.chan->center_freq);

	wl1271_debug(DEBUG_MAC80211,
		     "mac80211 change chanctx %d (type %d) changed 0x%x",
		     channel, cfg80211_get_chandef_type(&ctx->def), changed);

	mutex_lock(&wl->mutex);

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	wl12xx_for_each_wlvif(wl, wlvif) {
		struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);

		rcu_read_lock();
		if (rcu_access_pointer(vif->chanctx_conf) != ctx) {
			rcu_read_unlock();
			continue;
		}
		rcu_read_unlock();

		/* start radar if needed */
		if (changed & IEEE80211_CHANCTX_CHANGE_RADAR &&
		    wlvif->bss_type == BSS_TYPE_AP_BSS &&
		    ctx->radar_enabled && !wlvif->radar_enabled &&
		    ctx->def.chan->dfs_state == NL80211_DFS_USABLE) {
			wl1271_debug(DEBUG_MAC80211, "Start radar detection");
			wlcore_hw_set_cac(wl, wlvif, true);
			wlvif->radar_enabled = true;
		}
	}

	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);
}

static int wlcore_op_assign_vif_chanctx(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_chanctx_conf *ctx)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	int channel = ieee80211_frequency_to_channel(
		ctx->def.chan->center_freq);
	int ret = -EINVAL;

	wl1271_debug(DEBUG_MAC80211,
		     "mac80211 assign chanctx (role %d) %d (type %d) (radar %d dfs_state %d)",
		     wlvif->role_id, channel,
		     cfg80211_get_chandef_type(&ctx->def),
		     ctx->radar_enabled, ctx->def.chan->dfs_state);

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	if (unlikely(!test_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags)))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	wlvif->band = ctx->def.chan->band;
	wlvif->channel = channel;
	wlvif->channel_type = cfg80211_get_chandef_type(&ctx->def);

	/* update default rates according to the band */
	wl1271_set_band_rate(wl, wlvif);

	if (ctx->radar_enabled &&
	    ctx->def.chan->dfs_state == NL80211_DFS_USABLE) {
		wl1271_debug(DEBUG_MAC80211, "Start radar detection");
		wlcore_hw_set_cac(wl, wlvif, true);
		wlvif->radar_enabled = true;
	}

	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);

	return 0;
}

static void wlcore_op_unassign_vif_chanctx(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   struct ieee80211_chanctx_conf *ctx)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	int ret;

	wl1271_debug(DEBUG_MAC80211,
		     "mac80211 unassign chanctx (role %d) %d (type %d)",
		     wlvif->role_id,
		     ieee80211_frequency_to_channel(ctx->def.chan->center_freq),
		     cfg80211_get_chandef_type(&ctx->def));

	wl1271_tx_flush(wl);

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	if (unlikely(!test_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags)))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	if (wlvif->radar_enabled) {
		wl1271_debug(DEBUG_MAC80211, "Stop radar detection");
		wlcore_hw_set_cac(wl, wlvif, false);
		wlvif->radar_enabled = false;
	}

	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);
}

static int __wlcore_switch_vif_chan(struct wl1271 *wl,
				    struct wl12xx_vif *wlvif,
				    struct ieee80211_chanctx_conf *new_ctx)
{
	int channel = ieee80211_frequency_to_channel(
		new_ctx->def.chan->center_freq);

	wl1271_debug(DEBUG_MAC80211,
		     "switch vif (role %d) %d -> %d chan_type: %d",
		     wlvif->role_id, wlvif->channel, channel,
		     cfg80211_get_chandef_type(&new_ctx->def));

	if (WARN_ON_ONCE(wlvif->bss_type != BSS_TYPE_AP_BSS))
		return 0;

	WARN_ON(!test_bit(WLVIF_FLAG_BEACON_DISABLED, &wlvif->flags));

	if (wlvif->radar_enabled) {
		wl1271_debug(DEBUG_MAC80211, "Stop radar detection");
		wlcore_hw_set_cac(wl, wlvif, false);
		wlvif->radar_enabled = false;
	}

	wlvif->band = new_ctx->def.chan->band;
	wlvif->channel = channel;
	wlvif->channel_type = cfg80211_get_chandef_type(&new_ctx->def);

	/* start radar if needed */
	if (new_ctx->radar_enabled) {
		wl1271_debug(DEBUG_MAC80211, "Start radar detection");
		wlcore_hw_set_cac(wl, wlvif, true);
		wlvif->radar_enabled = true;
	}

	return 0;
}

static int
wlcore_op_switch_vif_chanctx(struct ieee80211_hw *hw,
			     struct ieee80211_vif_chanctx_switch *vifs,
			     int n_vifs,
			     enum ieee80211_chanctx_switch_mode mode)
{
	struct wl1271 *wl = hw->priv;
	int i, ret;

	wl1271_debug(DEBUG_MAC80211,
		     "mac80211 switch chanctx n_vifs %d mode %d",
		     n_vifs, mode);

	mutex_lock(&wl->mutex);

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	for (i = 0; i < n_vifs; i++) {
		struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vifs[i].vif);

		ret = __wlcore_switch_vif_chan(wl, wlvif, vifs[i].new_ctx);
		if (ret)
			goto out_sleep;
	}
out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);

	return 0;
}

static int wl1271_op_conf_tx(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif, u16 queue,
			     const struct ieee80211_tx_queue_params *params)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	u8 ps_scheme;
	int ret = 0;

	if (wlcore_is_p2p_mgmt(wlvif))
		return 0;

	mutex_lock(&wl->mutex);

	wl1271_debug(DEBUG_MAC80211, "mac80211 conf tx %d", queue);

	if (params->uapsd)
		ps_scheme = CONF_PS_SCHEME_UPSD_TRIGGER;
	else
		ps_scheme = CONF_PS_SCHEME_LEGACY;

	if (!test_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	/*
	 * the txop is confed in units of 32us by the mac80211,
	 * we need us
	 */
	ret = wl1271_acx_ac_cfg(wl, wlvif, wl1271_tx_get_queue(queue),
				params->cw_min, params->cw_max,
				params->aifs, params->txop << 5);
	if (ret < 0)
		goto out_sleep;

	ret = wl1271_acx_tid_cfg(wl, wlvif, wl1271_tx_get_queue(queue),
				 CONF_CHANNEL_TYPE_EDCF,
				 wl1271_tx_get_queue(queue),
				 ps_scheme, CONF_ACK_POLICY_LEGACY,
				 0, 0);

out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static u64 wl1271_op_get_tsf(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif)
{

	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	u64 mactime = ULLONG_MAX;
	int ret;

	wl1271_debug(DEBUG_MAC80211, "mac80211 get tsf");

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	ret = wl12xx_acx_tsf_info(wl, wlvif, &mactime);
	if (ret < 0)
		goto out_sleep;

out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

out:
	mutex_unlock(&wl->mutex);
	return mactime;
}

static int wl1271_op_get_survey(struct ieee80211_hw *hw, int idx,
				struct survey_info *survey)
{
	struct ieee80211_conf *conf = &hw->conf;

	if (idx != 0)
		return -ENOENT;

	survey->channel = conf->chandef.chan;
	survey->filled = 0;
	return 0;
}

static int wl1271_allocate_sta(struct wl1271 *wl,
			     struct wl12xx_vif *wlvif,
			     struct ieee80211_sta *sta)
{
	struct wl1271_station *wl_sta;
	int ret;


	if (wl->active_sta_count >= wl->max_ap_stations) {
		wl1271_warning("could not allocate HLID - too much stations");
		return -EBUSY;
	}

	wl_sta = (struct wl1271_station *)sta->drv_priv;
	ret = wl12xx_allocate_link(wl, wlvif, &wl_sta->hlid);
	if (ret < 0) {
		wl1271_warning("could not allocate HLID - too many links");
		return -EBUSY;
	}

	/* use the previous security seq, if this is a recovery/resume */
	wl->links[wl_sta->hlid].total_freed_pkts = wl_sta->total_freed_pkts;

	set_bit(wl_sta->hlid, wlvif->ap.sta_hlid_map);
	memcpy(wl->links[wl_sta->hlid].addr, sta->addr, ETH_ALEN);
	wl->active_sta_count++;
	return 0;
}

void wl1271_free_sta(struct wl1271 *wl, struct wl12xx_vif *wlvif, u8 hlid)
{
	if (!test_bit(hlid, wlvif->ap.sta_hlid_map))
		return;

	clear_bit(hlid, wlvif->ap.sta_hlid_map);
	__clear_bit(hlid, &wl->ap_ps_map);
	__clear_bit(hlid, &wl->ap_fw_ps_map);

	/*
	 * save the last used PN in the private part of iee80211_sta,
	 * in case of recovery/suspend
	 */
	wlcore_save_freed_pkts_addr(wl, wlvif, hlid, wl->links[hlid].addr);

	wl12xx_free_link(wl, wlvif, &hlid);
	wl->active_sta_count--;

	/*
	 * rearm the tx watchdog when the last STA is freed - give the FW a
	 * chance to return STA-buffered packets before complaining.
	 */
	if (wl->active_sta_count == 0)
		wl12xx_rearm_tx_watchdog_locked(wl);
}

static int wl12xx_sta_add(struct wl1271 *wl,
			  struct wl12xx_vif *wlvif,
			  struct ieee80211_sta *sta)
{
	struct wl1271_station *wl_sta;
	int ret = 0;
	u8 hlid;

	wl1271_debug(DEBUG_MAC80211, "mac80211 add sta %d", (int)sta->aid);

	ret = wl1271_allocate_sta(wl, wlvif, sta);
	if (ret < 0)
		return ret;

	wl_sta = (struct wl1271_station *)sta->drv_priv;
	hlid = wl_sta->hlid;

	ret = wl12xx_cmd_add_peer(wl, wlvif, sta, hlid);
	if (ret < 0)
		wl1271_free_sta(wl, wlvif, hlid);

	return ret;
}

static int wl12xx_sta_remove(struct wl1271 *wl,
			     struct wl12xx_vif *wlvif,
			     struct ieee80211_sta *sta)
{
	struct wl1271_station *wl_sta;
	int ret = 0, id;

	wl1271_debug(DEBUG_MAC80211, "mac80211 remove sta %d", (int)sta->aid);

	wl_sta = (struct wl1271_station *)sta->drv_priv;
	id = wl_sta->hlid;
	if (WARN_ON(!test_bit(id, wlvif->ap.sta_hlid_map)))
		return -EINVAL;

	ret = wl12xx_cmd_remove_peer(wl, wlvif, wl_sta->hlid);
	if (ret < 0)
		return ret;

	wl1271_free_sta(wl, wlvif, wl_sta->hlid);
	return ret;
}

static void wlcore_roc_if_possible(struct wl1271 *wl,
				   struct wl12xx_vif *wlvif)
{
	if (find_first_bit(wl->roc_map,
			   WL12XX_MAX_ROLES) < WL12XX_MAX_ROLES)
		return;

	if (WARN_ON(wlvif->role_id == WL12XX_INVALID_ROLE_ID))
		return;

	wl12xx_roc(wl, wlvif, wlvif->role_id, wlvif->band, wlvif->channel);
}

/*
 * when wl_sta is NULL, we treat this call as if coming from a
 * pending auth reply.
 * wl->mutex must be taken and the FW must be awake when the call
 * takes place.
 */
void wlcore_update_inconn_sta(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			      struct wl1271_station *wl_sta, bool in_conn)
{
	if (in_conn) {
		if (WARN_ON(wl_sta && wl_sta->in_connection))
			return;

		if (!wlvif->ap_pending_auth_reply &&
		    !wlvif->inconn_count)
			wlcore_roc_if_possible(wl, wlvif);

		if (wl_sta) {
			wl_sta->in_connection = true;
			wlvif->inconn_count++;
		} else {
			wlvif->ap_pending_auth_reply = true;
		}
	} else {
		if (wl_sta && !wl_sta->in_connection)
			return;

		if (WARN_ON(!wl_sta && !wlvif->ap_pending_auth_reply))
			return;

		if (WARN_ON(wl_sta && !wlvif->inconn_count))
			return;

		if (wl_sta) {
			wl_sta->in_connection = false;
			wlvif->inconn_count--;
		} else {
			wlvif->ap_pending_auth_reply = false;
		}

		if (!wlvif->inconn_count && !wlvif->ap_pending_auth_reply &&
		    test_bit(wlvif->role_id, wl->roc_map))
			wl12xx_croc(wl, wlvif->role_id);
	}
}

static int wl12xx_update_sta_state(struct wl1271 *wl,
				   struct wl12xx_vif *wlvif,
				   struct ieee80211_sta *sta,
				   enum ieee80211_sta_state old_state,
				   enum ieee80211_sta_state new_state)
{
	struct wl1271_station *wl_sta;
	bool is_ap = wlvif->bss_type == BSS_TYPE_AP_BSS;
	bool is_sta = wlvif->bss_type == BSS_TYPE_STA_BSS;
	int ret;

	wl_sta = (struct wl1271_station *)sta->drv_priv;

	/* Add station (AP mode) */
	if (is_ap &&
	    old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE) {
		ret = wl12xx_sta_add(wl, wlvif, sta);
		if (ret)
			return ret;

		wlcore_update_inconn_sta(wl, wlvif, wl_sta, true);
	}

	/* Remove station (AP mode) */
	if (is_ap &&
	    old_state == IEEE80211_STA_NONE &&
	    new_state == IEEE80211_STA_NOTEXIST) {
		/* must not fail */
		wl12xx_sta_remove(wl, wlvif, sta);

		wlcore_update_inconn_sta(wl, wlvif, wl_sta, false);
	}

	/* Authorize station (AP mode) */
	if (is_ap &&
	    new_state == IEEE80211_STA_AUTHORIZED) {
		ret = wl12xx_cmd_set_peer_state(wl, wlvif, wl_sta->hlid);
		if (ret < 0)
			return ret;

		/* reconfigure rates */
		ret = wl12xx_cmd_add_peer(wl, wlvif, sta, wl_sta->hlid);
		if (ret < 0)
			return ret;

		ret = wl1271_acx_set_ht_capabilities(wl, &sta->ht_cap, true,
						     wl_sta->hlid);
		if (ret)
			return ret;

		wlcore_update_inconn_sta(wl, wlvif, wl_sta, false);
	}

	/* Authorize station */
	if (is_sta &&
	    new_state == IEEE80211_STA_AUTHORIZED) {
		set_bit(WLVIF_FLAG_STA_AUTHORIZED, &wlvif->flags);
		ret = wl12xx_set_authorized(wl, wlvif);
		if (ret)
			return ret;
	}

	if (is_sta &&
	    old_state == IEEE80211_STA_AUTHORIZED &&
	    new_state == IEEE80211_STA_ASSOC) {
		clear_bit(WLVIF_FLAG_STA_AUTHORIZED, &wlvif->flags);
		clear_bit(WLVIF_FLAG_STA_STATE_SENT, &wlvif->flags);
	}

	/* save seq number on disassoc (suspend) */
	if (is_sta &&
	    old_state == IEEE80211_STA_ASSOC &&
	    new_state == IEEE80211_STA_AUTH) {
		wlcore_save_freed_pkts(wl, wlvif, wlvif->sta.hlid, sta);
		wlvif->total_freed_pkts = 0;
	}

	/* restore seq number on assoc (resume) */
	if (is_sta &&
	    old_state == IEEE80211_STA_AUTH &&
	    new_state == IEEE80211_STA_ASSOC) {
		wlvif->total_freed_pkts = wl_sta->total_freed_pkts;
	}

	/* clear ROCs on failure or authorization */
	if (is_sta &&
	    (new_state == IEEE80211_STA_AUTHORIZED ||
	     new_state == IEEE80211_STA_NOTEXIST)) {
		if (test_bit(wlvif->role_id, wl->roc_map))
			wl12xx_croc(wl, wlvif->role_id);
	}

	if (is_sta &&
	    old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE) {
		if (find_first_bit(wl->roc_map,
				   WL12XX_MAX_ROLES) >= WL12XX_MAX_ROLES) {
			WARN_ON(wlvif->role_id == WL12XX_INVALID_ROLE_ID);
			wl12xx_roc(wl, wlvif, wlvif->role_id,
				   wlvif->band, wlvif->channel);
		}
	}
	return 0;
}

static int wl12xx_op_sta_state(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       enum ieee80211_sta_state old_state,
			       enum ieee80211_sta_state new_state)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	int ret;

	wl1271_debug(DEBUG_MAC80211, "mac80211 sta %d state=%d->%d",
		     sta->aid, old_state, new_state);

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		ret = -EBUSY;
		goto out;
	}

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	ret = wl12xx_update_sta_state(wl, wlvif, sta, old_state, new_state);

	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);
	if (new_state < old_state)
		return 0;
	return ret;
}

static int wl1271_op_ampdu_action(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_ampdu_params *params)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	int ret;
	u8 hlid, *ba_bitmap;
	struct ieee80211_sta *sta = params->sta;
	enum ieee80211_ampdu_mlme_action action = params->action;
	u16 tid = params->tid;
	u16 *ssn = &params->ssn;

	wl1271_debug(DEBUG_MAC80211, "mac80211 ampdu action %d tid %d", action,
		     tid);

	/* sanity check - the fields in FW are only 8bits wide */
	if (WARN_ON(tid > 0xFF))
		return -ENOTSUPP;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		ret = -EAGAIN;
		goto out;
	}

	if (wlvif->bss_type == BSS_TYPE_STA_BSS) {
		hlid = wlvif->sta.hlid;
	} else if (wlvif->bss_type == BSS_TYPE_AP_BSS) {
		struct wl1271_station *wl_sta;

		wl_sta = (struct wl1271_station *)sta->drv_priv;
		hlid = wl_sta->hlid;
	} else {
		ret = -EINVAL;
		goto out;
	}

	ba_bitmap = &wl->links[hlid].ba_bitmap;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	wl1271_debug(DEBUG_MAC80211, "mac80211 ampdu: Rx tid %d action %d",
		     tid, action);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		if (!wlvif->ba_support || !wlvif->ba_allowed) {
			ret = -ENOTSUPP;
			break;
		}

		if (wl->ba_rx_session_count >= wl->ba_rx_session_count_max) {
			ret = -EBUSY;
			wl1271_error("exceeded max RX BA sessions");
			break;
		}

		if (*ba_bitmap & BIT(tid)) {
			ret = -EINVAL;
			wl1271_error("cannot enable RX BA session on active "
				     "tid: %d", tid);
			break;
		}

		ret = wl12xx_acx_set_ba_receiver_session(wl, tid, *ssn, true,
				hlid,
				params->buf_size);

		if (!ret) {
			*ba_bitmap |= BIT(tid);
			wl->ba_rx_session_count++;
		}
		break;

	case IEEE80211_AMPDU_RX_STOP:
		if (!(*ba_bitmap & BIT(tid))) {
			/*
			 * this happens on reconfig - so only output a debug
			 * message for now, and don't fail the function.
			 */
			wl1271_debug(DEBUG_MAC80211,
				     "no active RX BA session on tid: %d",
				     tid);
			ret = 0;
			break;
		}

		ret = wl12xx_acx_set_ba_receiver_session(wl, tid, 0, false,
							 hlid, 0);
		if (!ret) {
			*ba_bitmap &= ~BIT(tid);
			wl->ba_rx_session_count--;
		}
		break;

	/*
	 * The BA initiator session management in FW independently.
	 * Falling break here on purpose for all TX APDU commands.
	 */
	case IEEE80211_AMPDU_TX_START:
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		ret = -EINVAL;
		break;

	default:
		wl1271_error("Incorrect ampdu action id=%x\n", action);
		ret = -EINVAL;
	}

	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static int wl12xx_set_bitrate_mask(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   const struct cfg80211_bitrate_mask *mask)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	struct wl1271 *wl = hw->priv;
	int i, ret = 0;

	wl1271_debug(DEBUG_MAC80211, "mac80211 set_bitrate_mask 0x%x 0x%x",
		mask->control[NL80211_BAND_2GHZ].legacy,
		mask->control[NL80211_BAND_5GHZ].legacy);

	mutex_lock(&wl->mutex);

	for (i = 0; i < WLCORE_NUM_BANDS; i++)
		wlvif->bitrate_masks[i] =
			wl1271_tx_enabled_rates_get(wl,
						    mask->control[i].legacy,
						    i);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	if (wlvif->bss_type == BSS_TYPE_STA_BSS &&
	    !test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags)) {

		ret = pm_runtime_get_sync(wl->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(wl->dev);
			goto out;
		}

		wl1271_set_band_rate(wl, wlvif);
		wlvif->basic_rate =
			wl1271_tx_min_rate_get(wl, wlvif->basic_rate_set);
		ret = wl1271_acx_sta_rate_policies(wl, wlvif);

		pm_runtime_mark_last_busy(wl->dev);
		pm_runtime_put_autosuspend(wl->dev);
	}
out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static void wl12xx_op_channel_switch(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_channel_switch *ch_switch)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	int ret;

	wl1271_debug(DEBUG_MAC80211, "mac80211 channel switch");

	wl1271_tx_flush(wl);

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WLCORE_STATE_OFF)) {
		if (test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
			ieee80211_chswitch_done(vif, false);
		goto out;
	} else if (unlikely(wl->state != WLCORE_STATE_ON)) {
		goto out;
	}

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	/* TODO: change mac80211 to pass vif as param */

	if (test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags)) {
		unsigned long delay_usec;

		ret = wl->ops->channel_switch(wl, wlvif, ch_switch);
		if (ret)
			goto out_sleep;

		set_bit(WLVIF_FLAG_CS_PROGRESS, &wlvif->flags);

		/* indicate failure 5 seconds after channel switch time */
		delay_usec = ieee80211_tu_to_usec(wlvif->beacon_int) *
			ch_switch->count;
		ieee80211_queue_delayed_work(hw, &wlvif->channel_switch_work,
					     usecs_to_jiffies(delay_usec) +
					     msecs_to_jiffies(5000));
	}

out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

out:
	mutex_unlock(&wl->mutex);
}

static const void *wlcore_get_beacon_ie(struct wl1271 *wl,
					struct wl12xx_vif *wlvif,
					u8 eid)
{
	int ieoffset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	struct sk_buff *beacon =
		ieee80211_beacon_get(wl->hw, wl12xx_wlvif_to_vif(wlvif));

	if (!beacon)
		return NULL;

	return cfg80211_find_ie(eid,
				beacon->data + ieoffset,
				beacon->len - ieoffset);
}

static int wlcore_get_csa_count(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				u8 *csa_count)
{
	const u8 *ie;
	const struct ieee80211_channel_sw_ie *ie_csa;

	ie = wlcore_get_beacon_ie(wl, wlvif, WLAN_EID_CHANNEL_SWITCH);
	if (!ie)
		return -EINVAL;

	ie_csa = (struct ieee80211_channel_sw_ie *)&ie[2];
	*csa_count = ie_csa->count;

	return 0;
}

static void wlcore_op_channel_switch_beacon(struct ieee80211_hw *hw,
					    struct ieee80211_vif *vif,
					    struct cfg80211_chan_def *chandef)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	struct ieee80211_channel_switch ch_switch = {
		.block_tx = true,
		.chandef = *chandef,
	};
	int ret;

	wl1271_debug(DEBUG_MAC80211,
		     "mac80211 channel switch beacon (role %d)",
		     wlvif->role_id);

	ret = wlcore_get_csa_count(wl, wlvif, &ch_switch.count);
	if (ret < 0) {
		wl1271_error("error getting beacon (for CSA counter)");
		return;
	}

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		ret = -EBUSY;
		goto out;
	}

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	ret = wl->ops->channel_switch(wl, wlvif, &ch_switch);
	if (ret)
		goto out_sleep;

	set_bit(WLVIF_FLAG_CS_PROGRESS, &wlvif->flags);

out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);
}

static void wlcore_op_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    u32 queues, bool drop)
{
	struct wl1271 *wl = hw->priv;

	wl1271_tx_flush(wl);
}

static int wlcore_op_remain_on_channel(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_channel *chan,
				       int duration,
				       enum ieee80211_roc_type type)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	struct wl1271 *wl = hw->priv;
	int channel, active_roc, ret = 0;

	channel = ieee80211_frequency_to_channel(chan->center_freq);

	wl1271_debug(DEBUG_MAC80211, "mac80211 roc %d (%d)",
		     channel, wlvif->role_id);

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	/* return EBUSY if we can't ROC right now */
	active_roc = find_first_bit(wl->roc_map, WL12XX_MAX_ROLES);
	if (wl->roc_vif || active_roc < WL12XX_MAX_ROLES) {
		wl1271_warning("active roc on role %d", active_roc);
		ret = -EBUSY;
		goto out;
	}

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	ret = wl12xx_start_dev(wl, wlvif, chan->band, channel);
	if (ret < 0)
		goto out_sleep;

	wl->roc_vif = vif;
	ieee80211_queue_delayed_work(hw, &wl->roc_complete_work,
				     msecs_to_jiffies(duration));
out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);
	return ret;
}

static int __wlcore_roc_completed(struct wl1271 *wl)
{
	struct wl12xx_vif *wlvif;
	int ret;

	/* already completed */
	if (unlikely(!wl->roc_vif))
		return 0;

	wlvif = wl12xx_vif_to_data(wl->roc_vif);

	if (!test_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags))
		return -EBUSY;

	ret = wl12xx_stop_dev(wl, wlvif);
	if (ret < 0)
		return ret;

	wl->roc_vif = NULL;

	return 0;
}

static int wlcore_roc_completed(struct wl1271 *wl)
{
	int ret;

	wl1271_debug(DEBUG_MAC80211, "roc complete");

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		ret = -EBUSY;
		goto out;
	}

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	ret = __wlcore_roc_completed(wl);

	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static void wlcore_roc_complete_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wl1271 *wl;
	int ret;

	dwork = to_delayed_work(work);
	wl = container_of(dwork, struct wl1271, roc_complete_work);

	ret = wlcore_roc_completed(wl);
	if (!ret)
		ieee80211_remain_on_channel_expired(wl->hw);
}

static int wlcore_op_cancel_remain_on_channel(struct ieee80211_hw *hw,
					      struct ieee80211_vif *vif)
{
	struct wl1271 *wl = hw->priv;

	wl1271_debug(DEBUG_MAC80211, "mac80211 croc");

	/* TODO: per-vif */
	wl1271_tx_flush(wl);

	/*
	 * we can't just flush_work here, because it might deadlock
	 * (as we might get called from the same workqueue)
	 */
	cancel_delayed_work_sync(&wl->roc_complete_work);
	wlcore_roc_completed(wl);

	return 0;
}

static void wlcore_op_sta_rc_update(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta,
				    u32 changed)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);

	wl1271_debug(DEBUG_MAC80211, "mac80211 sta_rc_update");

	if (!(changed & IEEE80211_RC_BW_CHANGED))
		return;

	/* this callback is atomic, so schedule a new work */
	wlvif->rc_update_bw = sta->bandwidth;
	memcpy(&wlvif->rc_ht_cap, &sta->ht_cap, sizeof(sta->ht_cap));
	ieee80211_queue_work(hw, &wlvif->rc_update_work);
}

static void wlcore_op_sta_statistics(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta,
				     struct station_info *sinfo)
{
	struct wl1271 *wl = hw->priv;
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	s8 rssi_dbm;
	int ret;

	wl1271_debug(DEBUG_MAC80211, "mac80211 get_rssi");

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out_sleep;
	}

	ret = wlcore_acx_average_rssi(wl, wlvif, &rssi_dbm);
	if (ret < 0)
		goto out_sleep;

	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL);
	sinfo->signal = rssi_dbm;

out_sleep:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

out:
	mutex_unlock(&wl->mutex);
}

static u32 wlcore_op_get_expected_throughput(struct ieee80211_hw *hw,
					     struct ieee80211_sta *sta)
{
	struct wl1271_station *wl_sta = (struct wl1271_station *)sta->drv_priv;
	struct wl1271 *wl = hw->priv;
	u8 hlid = wl_sta->hlid;

	/* return in units of Kbps */
	return (wl->links[hlid].fw_rate_mbps * 1000);
}

static bool wl1271_tx_frames_pending(struct ieee80211_hw *hw)
{
	struct wl1271 *wl = hw->priv;
	bool ret = false;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	/* packets are considered pending if in the TX queue or the FW */
	ret = (wl1271_tx_total_queue_count(wl) > 0) || (wl->tx_frames_cnt > 0);
out:
	mutex_unlock(&wl->mutex);

	return ret;
}

/* can't be const, mac80211 writes to this */
static struct ieee80211_rate wl1271_rates[] = {
	{ .bitrate = 10,
	  .hw_value = CONF_HW_BIT_RATE_1MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_1MBPS, },
	{ .bitrate = 20,
	  .hw_value = CONF_HW_BIT_RATE_2MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_2MBPS,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55,
	  .hw_value = CONF_HW_BIT_RATE_5_5MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_5_5MBPS,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110,
	  .hw_value = CONF_HW_BIT_RATE_11MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_11MBPS,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60,
	  .hw_value = CONF_HW_BIT_RATE_6MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_6MBPS, },
	{ .bitrate = 90,
	  .hw_value = CONF_HW_BIT_RATE_9MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_9MBPS, },
	{ .bitrate = 120,
	  .hw_value = CONF_HW_BIT_RATE_12MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_12MBPS, },
	{ .bitrate = 180,
	  .hw_value = CONF_HW_BIT_RATE_18MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_18MBPS, },
	{ .bitrate = 240,
	  .hw_value = CONF_HW_BIT_RATE_24MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_24MBPS, },
	{ .bitrate = 360,
	 .hw_value = CONF_HW_BIT_RATE_36MBPS,
	 .hw_value_short = CONF_HW_BIT_RATE_36MBPS, },
	{ .bitrate = 480,
	  .hw_value = CONF_HW_BIT_RATE_48MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_48MBPS, },
	{ .bitrate = 540,
	  .hw_value = CONF_HW_BIT_RATE_54MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_54MBPS, },
};

/* can't be const, mac80211 writes to this */
static struct ieee80211_channel wl1271_channels[] = {
	{ .hw_value = 1, .center_freq = 2412, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 2, .center_freq = 2417, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 3, .center_freq = 2422, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 4, .center_freq = 2427, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 5, .center_freq = 2432, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 6, .center_freq = 2437, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 7, .center_freq = 2442, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 8, .center_freq = 2447, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 9, .center_freq = 2452, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 10, .center_freq = 2457, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 11, .center_freq = 2462, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 12, .center_freq = 2467, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 13, .center_freq = 2472, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 14, .center_freq = 2484, .max_power = WLCORE_MAX_TXPWR },
};

/* can't be const, mac80211 writes to this */
static struct ieee80211_supported_band wl1271_band_2ghz = {
	.channels = wl1271_channels,
	.n_channels = ARRAY_SIZE(wl1271_channels),
	.bitrates = wl1271_rates,
	.n_bitrates = ARRAY_SIZE(wl1271_rates),
};

/* 5 GHz data rates for WL1273 */
static struct ieee80211_rate wl1271_rates_5ghz[] = {
	{ .bitrate = 60,
	  .hw_value = CONF_HW_BIT_RATE_6MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_6MBPS, },
	{ .bitrate = 90,
	  .hw_value = CONF_HW_BIT_RATE_9MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_9MBPS, },
	{ .bitrate = 120,
	  .hw_value = CONF_HW_BIT_RATE_12MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_12MBPS, },
	{ .bitrate = 180,
	  .hw_value = CONF_HW_BIT_RATE_18MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_18MBPS, },
	{ .bitrate = 240,
	  .hw_value = CONF_HW_BIT_RATE_24MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_24MBPS, },
	{ .bitrate = 360,
	 .hw_value = CONF_HW_BIT_RATE_36MBPS,
	 .hw_value_short = CONF_HW_BIT_RATE_36MBPS, },
	{ .bitrate = 480,
	  .hw_value = CONF_HW_BIT_RATE_48MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_48MBPS, },
	{ .bitrate = 540,
	  .hw_value = CONF_HW_BIT_RATE_54MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_54MBPS, },
};

/* 5 GHz band channels for WL1273 */
static struct ieee80211_channel wl1271_channels_5ghz[] = {
	{ .hw_value = 8, .center_freq = 5040, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 12, .center_freq = 5060, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 16, .center_freq = 5080, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 34, .center_freq = 5170, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 36, .center_freq = 5180, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 38, .center_freq = 5190, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 40, .center_freq = 5200, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 42, .center_freq = 5210, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 44, .center_freq = 5220, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 46, .center_freq = 5230, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 48, .center_freq = 5240, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 52, .center_freq = 5260, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 56, .center_freq = 5280, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 60, .center_freq = 5300, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 64, .center_freq = 5320, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 100, .center_freq = 5500, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 104, .center_freq = 5520, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 108, .center_freq = 5540, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 112, .center_freq = 5560, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 116, .center_freq = 5580, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 120, .center_freq = 5600, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 124, .center_freq = 5620, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 128, .center_freq = 5640, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 132, .center_freq = 5660, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 136, .center_freq = 5680, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 140, .center_freq = 5700, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 149, .center_freq = 5745, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 153, .center_freq = 5765, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 157, .center_freq = 5785, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 161, .center_freq = 5805, .max_power = WLCORE_MAX_TXPWR },
	{ .hw_value = 165, .center_freq = 5825, .max_power = WLCORE_MAX_TXPWR },
};

static struct ieee80211_supported_band wl1271_band_5ghz = {
	.channels = wl1271_channels_5ghz,
	.n_channels = ARRAY_SIZE(wl1271_channels_5ghz),
	.bitrates = wl1271_rates_5ghz,
	.n_bitrates = ARRAY_SIZE(wl1271_rates_5ghz),
};

static const struct ieee80211_ops wl1271_ops = {
	.start = wl1271_op_start,
	.stop = wlcore_op_stop,
	.add_interface = wl1271_op_add_interface,
	.remove_interface = wl1271_op_remove_interface,
	.change_interface = wl12xx_op_change_interface,
#ifdef CONFIG_PM
	.suspend = wl1271_op_suspend,
	.resume = wl1271_op_resume,
#endif
	.config = wl1271_op_config,
	.prepare_multicast = wl1271_op_prepare_multicast,
	.configure_filter = wl1271_op_configure_filter,
	.tx = wl1271_op_tx,
	.set_key = wlcore_op_set_key,
	.hw_scan = wl1271_op_hw_scan,
	.cancel_hw_scan = wl1271_op_cancel_hw_scan,
	.sched_scan_start = wl1271_op_sched_scan_start,
	.sched_scan_stop = wl1271_op_sched_scan_stop,
	.bss_info_changed = wl1271_op_bss_info_changed,
	.set_frag_threshold = wl1271_op_set_frag_threshold,
	.set_rts_threshold = wl1271_op_set_rts_threshold,
	.conf_tx = wl1271_op_conf_tx,
	.get_tsf = wl1271_op_get_tsf,
	.get_survey = wl1271_op_get_survey,
	.sta_state = wl12xx_op_sta_state,
	.ampdu_action = wl1271_op_ampdu_action,
	.tx_frames_pending = wl1271_tx_frames_pending,
	.set_bitrate_mask = wl12xx_set_bitrate_mask,
	.set_default_unicast_key = wl1271_op_set_default_key_idx,
	.channel_switch = wl12xx_op_channel_switch,
	.channel_switch_beacon = wlcore_op_channel_switch_beacon,
	.flush = wlcore_op_flush,
	.remain_on_channel = wlcore_op_remain_on_channel,
	.cancel_remain_on_channel = wlcore_op_cancel_remain_on_channel,
	.add_chanctx = wlcore_op_add_chanctx,
	.remove_chanctx = wlcore_op_remove_chanctx,
	.change_chanctx = wlcore_op_change_chanctx,
	.assign_vif_chanctx = wlcore_op_assign_vif_chanctx,
	.unassign_vif_chanctx = wlcore_op_unassign_vif_chanctx,
	.switch_vif_chanctx = wlcore_op_switch_vif_chanctx,
	.sta_rc_update = wlcore_op_sta_rc_update,
	.sta_statistics = wlcore_op_sta_statistics,
	.get_expected_throughput = wlcore_op_get_expected_throughput,
	CFG80211_TESTMODE_CMD(wl1271_tm_cmd)
};


u8 wlcore_rate_to_idx(struct wl1271 *wl, u8 rate, enum nl80211_band band)
{
	u8 idx;

	BUG_ON(band >= 2);

	if (unlikely(rate >= wl->hw_tx_rate_tbl_size)) {
		wl1271_error("Illegal RX rate from HW: %d", rate);
		return 0;
	}

	idx = wl->band_rate_to_idx[band][rate];
	if (unlikely(idx == CONF_HW_RXTX_RATE_UNSUPPORTED)) {
		wl1271_error("Unsupported RX rate from HW: %d", rate);
		return 0;
	}

	return idx;
}

static void wl12xx_derive_mac_addresses(struct wl1271 *wl, u32 oui, u32 nic)
{
	int i;

	wl1271_debug(DEBUG_PROBE, "base address: oui %06x nic %06x",
		     oui, nic);

	if (nic + WLCORE_NUM_MAC_ADDRESSES - wl->num_mac_addr > 0xffffff)
		wl1271_warning("NIC part of the MAC address wraps around!");

	for (i = 0; i < wl->num_mac_addr; i++) {
		wl->addresses[i].addr[0] = (u8)(oui >> 16);
		wl->addresses[i].addr[1] = (u8)(oui >> 8);
		wl->addresses[i].addr[2] = (u8) oui;
		wl->addresses[i].addr[3] = (u8)(nic >> 16);
		wl->addresses[i].addr[4] = (u8)(nic >> 8);
		wl->addresses[i].addr[5] = (u8) nic;
		nic++;
	}

	/* we may be one address short at the most */
	WARN_ON(wl->num_mac_addr + 1 < WLCORE_NUM_MAC_ADDRESSES);

	/*
	 * turn on the LAA bit in the first address and use it as
	 * the last address.
	 */
	if (wl->num_mac_addr < WLCORE_NUM_MAC_ADDRESSES) {
		int idx = WLCORE_NUM_MAC_ADDRESSES - 1;
		memcpy(&wl->addresses[idx], &wl->addresses[0],
		       sizeof(wl->addresses[0]));
		/* LAA bit */
		wl->addresses[idx].addr[0] |= BIT(1);
	}

	wl->hw->wiphy->n_addresses = WLCORE_NUM_MAC_ADDRESSES;
	wl->hw->wiphy->addresses = wl->addresses;
}

static int wl12xx_get_hw_info(struct wl1271 *wl)
{
	int ret;

	ret = wlcore_read_reg(wl, REG_CHIP_ID_B, &wl->chip.id);
	if (ret < 0)
		goto out;

	wl->fuse_oui_addr = 0;
	wl->fuse_nic_addr = 0;

	ret = wl->ops->get_pg_ver(wl, &wl->hw_pg_ver);
	if (ret < 0)
		goto out;

	if (wl->ops->get_mac)
		ret = wl->ops->get_mac(wl);

out:
	return ret;
}

static int wl1271_register_hw(struct wl1271 *wl)
{
	int ret;
	u32 oui_addr = 0, nic_addr = 0;
	struct platform_device *pdev = wl->pdev;
	struct wlcore_platdev_data *pdev_data = dev_get_platdata(&pdev->dev);

	if (wl->mac80211_registered)
		return 0;

	if (wl->nvs_len >= 12) {
		/* NOTE: The wl->nvs->nvs element must be first, in
		 * order to simplify the casting, we assume it is at
		 * the beginning of the wl->nvs structure.
		 */
		u8 *nvs_ptr = (u8 *)wl->nvs;

		oui_addr =
			(nvs_ptr[11] << 16) + (nvs_ptr[10] << 8) + nvs_ptr[6];
		nic_addr =
			(nvs_ptr[5] << 16) + (nvs_ptr[4] << 8) + nvs_ptr[3];
	}

	/* if the MAC address is zeroed in the NVS derive from fuse */
	if (oui_addr == 0 && nic_addr == 0) {
		oui_addr = wl->fuse_oui_addr;
		/* fuse has the BD_ADDR, the WLAN addresses are the next two */
		nic_addr = wl->fuse_nic_addr + 1;
	}

	if (oui_addr == 0xdeadbe && nic_addr == 0xef0000) {
		wl1271_warning("Detected unconfigured mac address in nvs, derive from fuse instead.");
		if (!strcmp(pdev_data->family->name, "wl18xx")) {
			wl1271_warning("This default nvs file can be removed from the file system");
		} else {
			wl1271_warning("Your device performance is not optimized.");
			wl1271_warning("Please use the calibrator tool to configure your device.");
		}

		if (wl->fuse_oui_addr == 0 && wl->fuse_nic_addr == 0) {
			wl1271_warning("Fuse mac address is zero. using random mac");
			/* Use TI oui and a random nic */
			oui_addr = WLCORE_TI_OUI_ADDRESS;
			nic_addr = get_random_int();
		} else {
			oui_addr = wl->fuse_oui_addr;
			/* fuse has the BD_ADDR, the WLAN addresses are the next two */
			nic_addr = wl->fuse_nic_addr + 1;
		}
	}

	wl12xx_derive_mac_addresses(wl, oui_addr, nic_addr);

	ret = ieee80211_register_hw(wl->hw);
	if (ret < 0) {
		wl1271_error("unable to register mac80211 hw: %d", ret);
		goto out;
	}

	wl->mac80211_registered = true;

	wl1271_debugfs_init(wl);

	wl1271_notice("loaded");

out:
	return ret;
}

static void wl1271_unregister_hw(struct wl1271 *wl)
{
	if (wl->plt)
		wl1271_plt_stop(wl);

	ieee80211_unregister_hw(wl->hw);
	wl->mac80211_registered = false;

}

static int wl1271_init_ieee80211(struct wl1271 *wl)
{
	int i;
	static const u32 cipher_suites[] = {
		WLAN_CIPHER_SUITE_WEP40,
		WLAN_CIPHER_SUITE_WEP104,
		WLAN_CIPHER_SUITE_TKIP,
		WLAN_CIPHER_SUITE_CCMP,
		WL1271_CIPHER_SUITE_GEM,
	};

	/* The tx descriptor buffer */
	wl->hw->extra_tx_headroom = sizeof(struct wl1271_tx_hw_descr);

	if (wl->quirks & WLCORE_QUIRK_TKIP_HEADER_SPACE)
		wl->hw->extra_tx_headroom += WL1271_EXTRA_SPACE_TKIP;

	/* unit us */
	/* FIXME: find a proper value */
	wl->hw->max_listen_interval = wl->conf.conn.max_listen_interval;

	ieee80211_hw_set(wl->hw, SUPPORT_FAST_XMIT);
	ieee80211_hw_set(wl->hw, CHANCTX_STA_CSA);
	ieee80211_hw_set(wl->hw, QUEUE_CONTROL);
	ieee80211_hw_set(wl->hw, TX_AMPDU_SETUP_IN_HW);
	ieee80211_hw_set(wl->hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(wl->hw, AP_LINK_PS);
	ieee80211_hw_set(wl->hw, SPECTRUM_MGMT);
	ieee80211_hw_set(wl->hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(wl->hw, CONNECTION_MONITOR);
	ieee80211_hw_set(wl->hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(wl->hw, SUPPORTS_DYNAMIC_PS);
	ieee80211_hw_set(wl->hw, SIGNAL_DBM);
	ieee80211_hw_set(wl->hw, SUPPORTS_PS);
	ieee80211_hw_set(wl->hw, SUPPORTS_TX_FRAG);

	wl->hw->wiphy->cipher_suites = cipher_suites;
	wl->hw->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);

	wl->hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
					 BIT(NL80211_IFTYPE_AP) |
					 BIT(NL80211_IFTYPE_P2P_DEVICE) |
					 BIT(NL80211_IFTYPE_P2P_CLIENT) |
#ifdef CONFIG_MAC80211_MESH
					 BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
					 BIT(NL80211_IFTYPE_P2P_GO);

	wl->hw->wiphy->max_scan_ssids = 1;
	wl->hw->wiphy->max_sched_scan_ssids = 16;
	wl->hw->wiphy->max_match_sets = 16;
	/*
	 * Maximum length of elements in scanning probe request templates
	 * should be the maximum length possible for a template, without
	 * the IEEE80211 header of the template
	 */
	wl->hw->wiphy->max_scan_ie_len = WL1271_CMD_TEMPL_MAX_SIZE -
			sizeof(struct ieee80211_header);

	wl->hw->wiphy->max_sched_scan_reqs = 1;
	wl->hw->wiphy->max_sched_scan_ie_len = WL1271_CMD_TEMPL_MAX_SIZE -
		sizeof(struct ieee80211_header);

	wl->hw->wiphy->max_remain_on_channel_duration = 30000;

	wl->hw->wiphy->flags |= WIPHY_FLAG_AP_UAPSD |
				WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
				WIPHY_FLAG_HAS_CHANNEL_SWITCH;

	wl->hw->wiphy->features |= NL80211_FEATURE_AP_SCAN;

	/* make sure all our channels fit in the scanned_ch bitmask */
	BUILD_BUG_ON(ARRAY_SIZE(wl1271_channels) +
		     ARRAY_SIZE(wl1271_channels_5ghz) >
		     WL1271_MAX_CHANNELS);
	/*
	* clear channel flags from the previous usage
	* and restore max_power & max_antenna_gain values.
	*/
	for (i = 0; i < ARRAY_SIZE(wl1271_channels); i++) {
		wl1271_band_2ghz.channels[i].flags = 0;
		wl1271_band_2ghz.channels[i].max_power = WLCORE_MAX_TXPWR;
		wl1271_band_2ghz.channels[i].max_antenna_gain = 0;
	}

	for (i = 0; i < ARRAY_SIZE(wl1271_channels_5ghz); i++) {
		wl1271_band_5ghz.channels[i].flags = 0;
		wl1271_band_5ghz.channels[i].max_power = WLCORE_MAX_TXPWR;
		wl1271_band_5ghz.channels[i].max_antenna_gain = 0;
	}

	/*
	 * We keep local copies of the band structs because we need to
	 * modify them on a per-device basis.
	 */
	memcpy(&wl->bands[NL80211_BAND_2GHZ], &wl1271_band_2ghz,
	       sizeof(wl1271_band_2ghz));
	memcpy(&wl->bands[NL80211_BAND_2GHZ].ht_cap,
	       &wl->ht_cap[NL80211_BAND_2GHZ],
	       sizeof(*wl->ht_cap));
	memcpy(&wl->bands[NL80211_BAND_5GHZ], &wl1271_band_5ghz,
	       sizeof(wl1271_band_5ghz));
	memcpy(&wl->bands[NL80211_BAND_5GHZ].ht_cap,
	       &wl->ht_cap[NL80211_BAND_5GHZ],
	       sizeof(*wl->ht_cap));

	wl->hw->wiphy->bands[NL80211_BAND_2GHZ] =
		&wl->bands[NL80211_BAND_2GHZ];
	wl->hw->wiphy->bands[NL80211_BAND_5GHZ] =
		&wl->bands[NL80211_BAND_5GHZ];

	/*
	 * allow 4 queues per mac address we support +
	 * 1 cab queue per mac + one global offchannel Tx queue
	 */
	wl->hw->queues = (NUM_TX_QUEUES + 1) * WLCORE_NUM_MAC_ADDRESSES + 1;

	/* the last queue is the offchannel queue */
	wl->hw->offchannel_tx_hw_queue = wl->hw->queues - 1;
	wl->hw->max_rates = 1;

	wl->hw->wiphy->reg_notifier = wl1271_reg_notify;

	/* the FW answers probe-requests in AP-mode */
	wl->hw->wiphy->flags |= WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD;
	wl->hw->wiphy->probe_resp_offload =
		NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS |
		NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS2 |
		NL80211_PROBE_RESP_OFFLOAD_SUPPORT_P2P;

	/* allowed interface combinations */
	wl->hw->wiphy->iface_combinations = wl->iface_combinations;
	wl->hw->wiphy->n_iface_combinations = wl->n_iface_combinations;

	/* register vendor commands */
	wlcore_set_vendor_commands(wl->hw->wiphy);

	SET_IEEE80211_DEV(wl->hw, wl->dev);

	wl->hw->sta_data_size = sizeof(struct wl1271_station);
	wl->hw->vif_data_size = sizeof(struct wl12xx_vif);

	wl->hw->max_rx_aggregation_subframes = wl->conf.ht.rx_ba_win_size;

	return 0;
}

struct ieee80211_hw *wlcore_alloc_hw(size_t priv_size, u32 aggr_buf_size,
				     u32 mbox_size)
{
	struct ieee80211_hw *hw;
	struct wl1271 *wl;
	int i, j, ret;
	unsigned int order;

	hw = ieee80211_alloc_hw(sizeof(*wl), &wl1271_ops);
	if (!hw) {
		wl1271_error("could not alloc ieee80211_hw");
		ret = -ENOMEM;
		goto err_hw_alloc;
	}

	wl = hw->priv;
	memset(wl, 0, sizeof(*wl));

	wl->priv = kzalloc(priv_size, GFP_KERNEL);
	if (!wl->priv) {
		wl1271_error("could not alloc wl priv");
		ret = -ENOMEM;
		goto err_priv_alloc;
	}

	INIT_LIST_HEAD(&wl->wlvif_list);

	wl->hw = hw;

	/*
	 * wl->num_links is not configured yet, so just use WLCORE_MAX_LINKS.
	 * we don't allocate any additional resource here, so that's fine.
	 */
	for (i = 0; i < NUM_TX_QUEUES; i++)
		for (j = 0; j < WLCORE_MAX_LINKS; j++)
			skb_queue_head_init(&wl->links[j].tx_queue[i]);

	skb_queue_head_init(&wl->deferred_rx_queue);
	skb_queue_head_init(&wl->deferred_tx_queue);

	INIT_WORK(&wl->netstack_work, wl1271_netstack_work);
	INIT_WORK(&wl->tx_work, wl1271_tx_work);
	INIT_WORK(&wl->recovery_work, wl1271_recovery_work);
	INIT_DELAYED_WORK(&wl->scan_complete_work, wl1271_scan_complete_work);
	INIT_DELAYED_WORK(&wl->roc_complete_work, wlcore_roc_complete_work);
	INIT_DELAYED_WORK(&wl->tx_watchdog_work, wl12xx_tx_watchdog_work);

	wl->freezable_wq = create_freezable_workqueue("wl12xx_wq");
	if (!wl->freezable_wq) {
		ret = -ENOMEM;
		goto err_hw;
	}

	wl->channel = 0;
	wl->rx_counter = 0;
	wl->power_level = WL1271_DEFAULT_POWER_LEVEL;
	wl->band = NL80211_BAND_2GHZ;
	wl->channel_type = NL80211_CHAN_NO_HT;
	wl->flags = 0;
	wl->sg_enabled = true;
	wl->sleep_auth = WL1271_PSM_ILLEGAL;
	wl->recovery_count = 0;
	wl->hw_pg_ver = -1;
	wl->ap_ps_map = 0;
	wl->ap_fw_ps_map = 0;
	wl->quirks = 0;
	wl->system_hlid = WL12XX_SYSTEM_HLID;
	wl->active_sta_count = 0;
	wl->active_link_count = 0;
	wl->fwlog_size = 0;

	/* The system link is always allocated */
	__set_bit(WL12XX_SYSTEM_HLID, wl->links_map);

	memset(wl->tx_frames_map, 0, sizeof(wl->tx_frames_map));
	for (i = 0; i < wl->num_tx_desc; i++)
		wl->tx_frames[i] = NULL;

	spin_lock_init(&wl->wl_lock);

	wl->state = WLCORE_STATE_OFF;
	wl->fw_type = WL12XX_FW_TYPE_NONE;
	mutex_init(&wl->mutex);
	mutex_init(&wl->flush_mutex);
	init_completion(&wl->nvs_loading_complete);

	order = get_order(aggr_buf_size);
	wl->aggr_buf = (u8 *)__get_free_pages(GFP_KERNEL, order);
	if (!wl->aggr_buf) {
		ret = -ENOMEM;
		goto err_wq;
	}
	wl->aggr_buf_size = aggr_buf_size;

	wl->dummy_packet = wl12xx_alloc_dummy_packet(wl);
	if (!wl->dummy_packet) {
		ret = -ENOMEM;
		goto err_aggr;
	}

	/* Allocate one page for the FW log */
	wl->fwlog = (u8 *)get_zeroed_page(GFP_KERNEL);
	if (!wl->fwlog) {
		ret = -ENOMEM;
		goto err_dummy_packet;
	}

	wl->mbox_size = mbox_size;
	wl->mbox = kmalloc(wl->mbox_size, GFP_KERNEL | GFP_DMA);
	if (!wl->mbox) {
		ret = -ENOMEM;
		goto err_fwlog;
	}

	wl->buffer_32 = kmalloc(sizeof(*wl->buffer_32), GFP_KERNEL);
	if (!wl->buffer_32) {
		ret = -ENOMEM;
		goto err_mbox;
	}

	return hw;

err_mbox:
	kfree(wl->mbox);

err_fwlog:
	free_page((unsigned long)wl->fwlog);

err_dummy_packet:
	dev_kfree_skb(wl->dummy_packet);

err_aggr:
	free_pages((unsigned long)wl->aggr_buf, order);

err_wq:
	destroy_workqueue(wl->freezable_wq);

err_hw:
	wl1271_debugfs_exit(wl);
	kfree(wl->priv);

err_priv_alloc:
	ieee80211_free_hw(hw);

err_hw_alloc:

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(wlcore_alloc_hw);

int wlcore_free_hw(struct wl1271 *wl)
{
	/* Unblock any fwlog readers */
	mutex_lock(&wl->mutex);
	wl->fwlog_size = -1;
	mutex_unlock(&wl->mutex);

	wlcore_sysfs_free(wl);

	kfree(wl->buffer_32);
	kfree(wl->mbox);
	free_page((unsigned long)wl->fwlog);
	dev_kfree_skb(wl->dummy_packet);
	free_pages((unsigned long)wl->aggr_buf, get_order(wl->aggr_buf_size));

	wl1271_debugfs_exit(wl);

	vfree(wl->fw);
	wl->fw = NULL;
	wl->fw_type = WL12XX_FW_TYPE_NONE;
	kfree(wl->nvs);
	wl->nvs = NULL;

	kfree(wl->raw_fw_status);
	kfree(wl->fw_status);
	kfree(wl->tx_res_if);
	destroy_workqueue(wl->freezable_wq);

	kfree(wl->priv);
	ieee80211_free_hw(wl->hw);

	return 0;
}
EXPORT_SYMBOL_GPL(wlcore_free_hw);

#ifdef CONFIG_PM
static const struct wiphy_wowlan_support wlcore_wowlan_support = {
	.flags = WIPHY_WOWLAN_ANY,
	.n_patterns = WL1271_MAX_RX_FILTERS,
	.pattern_min_len = 1,
	.pattern_max_len = WL1271_RX_FILTER_MAX_PATTERN_SIZE,
};
#endif

static irqreturn_t wlcore_hardirq(int irq, void *cookie)
{
	return IRQ_WAKE_THREAD;
}

static void wlcore_nvs_cb(const struct firmware *fw, void *context)
{
	struct wl1271 *wl = context;
	struct platform_device *pdev = wl->pdev;
	struct wlcore_platdev_data *pdev_data = dev_get_platdata(&pdev->dev);
	struct resource *res;

	int ret;
	irq_handler_t hardirq_fn = NULL;

	if (fw) {
		wl->nvs = kmemdup(fw->data, fw->size, GFP_KERNEL);
		if (!wl->nvs) {
			wl1271_error("Could not allocate nvs data");
			goto out;
		}
		wl->nvs_len = fw->size;
	} else if (pdev_data->family->nvs_name) {
		wl1271_debug(DEBUG_BOOT, "Could not get nvs file %s",
			     pdev_data->family->nvs_name);
		wl->nvs = NULL;
		wl->nvs_len = 0;
	} else {
		wl->nvs = NULL;
		wl->nvs_len = 0;
	}

	ret = wl->ops->setup(wl);
	if (ret < 0)
		goto out_free_nvs;

	BUG_ON(wl->num_tx_desc > WLCORE_MAX_TX_DESCRIPTORS);

	/* adjust some runtime configuration parameters */
	wlcore_adjust_conf(wl);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		wl1271_error("Could not get IRQ resource");
		goto out_free_nvs;
	}

	wl->irq = res->start;
	wl->irq_flags = res->flags & IRQF_TRIGGER_MASK;
	wl->if_ops = pdev_data->if_ops;

	if (wl->irq_flags & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING))
		hardirq_fn = wlcore_hardirq;
	else
		wl->irq_flags |= IRQF_ONESHOT;

	ret = wl12xx_set_power_on(wl);
	if (ret < 0)
		goto out_free_nvs;

	ret = wl12xx_get_hw_info(wl);
	if (ret < 0) {
		wl1271_error("couldn't get hw info");
		wl1271_power_off(wl);
		goto out_free_nvs;
	}

	ret = request_threaded_irq(wl->irq, hardirq_fn, wlcore_irq,
				   wl->irq_flags, pdev->name, wl);
	if (ret < 0) {
		wl1271_error("interrupt configuration failed");
		wl1271_power_off(wl);
		goto out_free_nvs;
	}

#ifdef CONFIG_PM
	device_init_wakeup(wl->dev, true);

	ret = enable_irq_wake(wl->irq);
	if (!ret) {
		wl->irq_wake_enabled = true;
		if (pdev_data->pwr_in_suspend)
			wl->hw->wiphy->wowlan = &wlcore_wowlan_support;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if (res) {
		wl->wakeirq = res->start;
		wl->wakeirq_flags = res->flags & IRQF_TRIGGER_MASK;
		ret = dev_pm_set_dedicated_wake_irq(wl->dev, wl->wakeirq);
		if (ret)
			wl->wakeirq = -ENODEV;
	} else {
		wl->wakeirq = -ENODEV;
	}
#endif
	disable_irq(wl->irq);
	wl1271_power_off(wl);

	ret = wl->ops->identify_chip(wl);
	if (ret < 0)
		goto out_irq;

	ret = wl1271_init_ieee80211(wl);
	if (ret)
		goto out_irq;

	ret = wl1271_register_hw(wl);
	if (ret)
		goto out_irq;

	ret = wlcore_sysfs_init(wl);
	if (ret)
		goto out_unreg;

	wl->initialized = true;
	goto out;

out_unreg:
	wl1271_unregister_hw(wl);

out_irq:
	if (wl->wakeirq >= 0)
		dev_pm_clear_wake_irq(wl->dev);
	device_init_wakeup(wl->dev, false);
	free_irq(wl->irq, wl);

out_free_nvs:
	kfree(wl->nvs);

out:
	release_firmware(fw);
	complete_all(&wl->nvs_loading_complete);
}

static int __maybe_unused wlcore_runtime_suspend(struct device *dev)
{
	struct wl1271 *wl = dev_get_drvdata(dev);
	struct wl12xx_vif *wlvif;
	int error;

	/* We do not enter elp sleep in PLT mode */
	if (wl->plt)
		return 0;

	/* Nothing to do if no ELP mode requested */
	if (wl->sleep_auth != WL1271_PSM_ELP)
		return 0;

	wl12xx_for_each_wlvif(wl, wlvif) {
		if (!test_bit(WLVIF_FLAG_IN_PS, &wlvif->flags) &&
		    test_bit(WLVIF_FLAG_IN_USE, &wlvif->flags))
			return -EBUSY;
	}

	wl1271_debug(DEBUG_PSM, "chip to elp");
	error = wlcore_raw_write32(wl, HW_ACCESS_ELP_CTRL_REG, ELPCTRL_SLEEP);
	if (error < 0) {
		wl12xx_queue_recovery_work(wl);

		return error;
	}

	set_bit(WL1271_FLAG_IN_ELP, &wl->flags);

	return 0;
}

static int __maybe_unused wlcore_runtime_resume(struct device *dev)
{
	struct wl1271 *wl = dev_get_drvdata(dev);
	DECLARE_COMPLETION_ONSTACK(compl);
	unsigned long flags;
	int ret;
	unsigned long start_time = jiffies;
	bool pending = false;
	bool recovery = false;

	/* Nothing to do if no ELP mode requested */
	if (!test_bit(WL1271_FLAG_IN_ELP, &wl->flags))
		return 0;

	wl1271_debug(DEBUG_PSM, "waking up chip from elp");

	spin_lock_irqsave(&wl->wl_lock, flags);
	if (test_bit(WL1271_FLAG_IRQ_RUNNING, &wl->flags))
		pending = true;
	else
		wl->elp_compl = &compl;
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	ret = wlcore_raw_write32(wl, HW_ACCESS_ELP_CTRL_REG, ELPCTRL_WAKE_UP);
	if (ret < 0) {
		recovery = true;
		goto err;
	}

	if (!pending) {
		ret = wait_for_completion_timeout(&compl,
			msecs_to_jiffies(WL1271_WAKEUP_TIMEOUT));
		if (ret == 0) {
			wl1271_warning("ELP wakeup timeout!");

			/* Return no error for runtime PM for recovery */
			ret = 0;
			recovery = true;
			goto err;
		}
	}

	clear_bit(WL1271_FLAG_IN_ELP, &wl->flags);

	wl1271_debug(DEBUG_PSM, "wakeup time: %u ms",
		     jiffies_to_msecs(jiffies - start_time));

	return 0;

err:
	spin_lock_irqsave(&wl->wl_lock, flags);
	wl->elp_compl = NULL;
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	if (recovery) {
		set_bit(WL1271_FLAG_INTENDED_FW_RECOVERY, &wl->flags);
		wl12xx_queue_recovery_work(wl);
	}

	return ret;
}

static const struct dev_pm_ops wlcore_pm_ops = {
	SET_RUNTIME_PM_OPS(wlcore_runtime_suspend,
			   wlcore_runtime_resume,
			   NULL)
};

int wlcore_probe(struct wl1271 *wl, struct platform_device *pdev)
{
	struct wlcore_platdev_data *pdev_data = dev_get_platdata(&pdev->dev);
	const char *nvs_name;
	int ret = 0;

	if (!wl->ops || !wl->ptable || !pdev_data)
		return -EINVAL;

	wl->dev = &pdev->dev;
	wl->pdev = pdev;
	platform_set_drvdata(pdev, wl);

	if (pdev_data->family && pdev_data->family->nvs_name) {
		nvs_name = pdev_data->family->nvs_name;
		ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
					      nvs_name, &pdev->dev, GFP_KERNEL,
					      wl, wlcore_nvs_cb);
		if (ret < 0) {
			wl1271_error("request_firmware_nowait failed for %s: %d",
				     nvs_name, ret);
			complete_all(&wl->nvs_loading_complete);
		}
	} else {
		wlcore_nvs_cb(NULL, wl);
	}

	wl->dev->driver->pm = &wlcore_pm_ops;
	pm_runtime_set_autosuspend_delay(wl->dev, 50);
	pm_runtime_use_autosuspend(wl->dev);
	pm_runtime_enable(wl->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(wlcore_probe);

int wlcore_remove(struct platform_device *pdev)
{
	struct wlcore_platdev_data *pdev_data = dev_get_platdata(&pdev->dev);
	struct wl1271 *wl = platform_get_drvdata(pdev);
	int error;

	error = pm_runtime_get_sync(wl->dev);
	if (error < 0)
		dev_warn(wl->dev, "PM runtime failed: %i\n", error);

	wl->dev->driver->pm = NULL;

	if (pdev_data->family && pdev_data->family->nvs_name)
		wait_for_completion(&wl->nvs_loading_complete);
	if (!wl->initialized)
		return 0;

	if (wl->wakeirq >= 0) {
		dev_pm_clear_wake_irq(wl->dev);
		wl->wakeirq = -ENODEV;
	}

	device_init_wakeup(wl->dev, false);

	if (wl->irq_wake_enabled)
		disable_irq_wake(wl->irq);

	wl1271_unregister_hw(wl);

	pm_runtime_put_sync(wl->dev);
	pm_runtime_dont_use_autosuspend(wl->dev);
	pm_runtime_disable(wl->dev);

	free_irq(wl->irq, wl);
	wlcore_free_hw(wl);

	return 0;
}
EXPORT_SYMBOL_GPL(wlcore_remove);

u32 wl12xx_debug_level = DEBUG_NONE;
EXPORT_SYMBOL_GPL(wl12xx_debug_level);
module_param_named(debug_level, wl12xx_debug_level, uint, 0600);
MODULE_PARM_DESC(debug_level, "wl12xx debugging level");

module_param_named(fwlog, fwlog_param, charp, 0);
MODULE_PARM_DESC(fwlog,
		 "FW logger options: continuous, dbgpins or disable");

module_param(fwlog_mem_blocks, int, 0600);
MODULE_PARM_DESC(fwlog_mem_blocks, "fwlog mem_blocks");

module_param(bug_on_recovery, int, 0600);
MODULE_PARM_DESC(bug_on_recovery, "BUG() on fw recovery");

module_param(no_recovery, int, 0600);
MODULE_PARM_DESC(no_recovery, "Prevent HW recovery. FW will remain stuck.");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luciano Coelho <coelho@ti.com>");
MODULE_AUTHOR("Juuso Oikarinen <juuso.oikarinen@nokia.com>");
