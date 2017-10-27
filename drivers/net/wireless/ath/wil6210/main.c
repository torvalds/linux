/*
 * Copyright (c) 2012-2017 Qualcomm Atheros, Inc.
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

#include <linux/moduleparam.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>

#include "wil6210.h"
#include "txrx.h"
#include "wmi.h"
#include "boot_loader.h"

#define WAIT_FOR_HALP_VOTE_MS 100
#define WAIT_FOR_SCAN_ABORT_MS 1000

bool debug_fw; /* = false; */
module_param(debug_fw, bool, 0444);
MODULE_PARM_DESC(debug_fw, " do not perform card reset. For FW debug");

static u8 oob_mode;
module_param(oob_mode, byte, 0444);
MODULE_PARM_DESC(oob_mode,
		 " enable out of the box (OOB) mode in FW, for diagnostics and certification");

bool no_fw_recovery;
module_param(no_fw_recovery, bool, 0644);
MODULE_PARM_DESC(no_fw_recovery, " disable automatic FW error recovery");

/* if not set via modparam, will be set to default value of 1/8 of
 * rx ring size during init flow
 */
unsigned short rx_ring_overflow_thrsh = WIL6210_RX_HIGH_TRSH_INIT;
module_param(rx_ring_overflow_thrsh, ushort, 0444);
MODULE_PARM_DESC(rx_ring_overflow_thrsh,
		 " RX ring overflow threshold in descriptors.");

/* We allow allocation of more than 1 page buffers to support large packets.
 * It is suboptimal behavior performance wise in case MTU above page size.
 */
unsigned int mtu_max = TXRX_BUF_LEN_DEFAULT - WIL_MAX_MPDU_OVERHEAD;
static int mtu_max_set(const char *val, const struct kernel_param *kp)
{
	int ret;

	/* sets mtu_max directly. no need to restore it in case of
	 * illegal value since we assume this will fail insmod
	 */
	ret = param_set_uint(val, kp);
	if (ret)
		return ret;

	if (mtu_max < 68 || mtu_max > WIL_MAX_ETH_MTU)
		ret = -EINVAL;

	return ret;
}

static const struct kernel_param_ops mtu_max_ops = {
	.set = mtu_max_set,
	.get = param_get_uint,
};

module_param_cb(mtu_max, &mtu_max_ops, &mtu_max, 0444);
MODULE_PARM_DESC(mtu_max, " Max MTU value.");

static uint rx_ring_order = WIL_RX_RING_SIZE_ORDER_DEFAULT;
static uint tx_ring_order = WIL_TX_RING_SIZE_ORDER_DEFAULT;
static uint bcast_ring_order = WIL_BCAST_RING_SIZE_ORDER_DEFAULT;

static int ring_order_set(const char *val, const struct kernel_param *kp)
{
	int ret;
	uint x;

	ret = kstrtouint(val, 0, &x);
	if (ret)
		return ret;

	if ((x < WIL_RING_SIZE_ORDER_MIN) || (x > WIL_RING_SIZE_ORDER_MAX))
		return -EINVAL;

	*((uint *)kp->arg) = x;

	return 0;
}

static const struct kernel_param_ops ring_order_ops = {
	.set = ring_order_set,
	.get = param_get_uint,
};

module_param_cb(rx_ring_order, &ring_order_ops, &rx_ring_order, 0444);
MODULE_PARM_DESC(rx_ring_order, " Rx ring order; size = 1 << order");
module_param_cb(tx_ring_order, &ring_order_ops, &tx_ring_order, 0444);
MODULE_PARM_DESC(tx_ring_order, " Tx ring order; size = 1 << order");
module_param_cb(bcast_ring_order, &ring_order_ops, &bcast_ring_order, 0444);
MODULE_PARM_DESC(bcast_ring_order, " Bcast ring order; size = 1 << order");

#define RST_DELAY (20) /* msec, for loop in @wil_target_reset */
#define RST_COUNT (1 + 1000/RST_DELAY) /* round up to be above 1 sec total */

/*
 * Due to a hardware issue,
 * one has to read/write to/from NIC in 32-bit chunks;
 * regular memcpy_fromio and siblings will
 * not work on 64-bit platform - it uses 64-bit transactions
 *
 * Force 32-bit transactions to enable NIC on 64-bit platforms
 *
 * To avoid byte swap on big endian host, __raw_{read|write}l
 * should be used - {read|write}l would swap bytes to provide
 * little endian on PCI value in host endianness.
 */
void wil_memcpy_fromio_32(void *dst, const volatile void __iomem *src,
			  size_t count)
{
	u32 *d = dst;
	const volatile u32 __iomem *s = src;

	for (; count >= 4; count -= 4)
		*d++ = __raw_readl(s++);

	if (unlikely(count)) {
		/* count can be 1..3 */
		u32 tmp = __raw_readl(s);

		memcpy(d, &tmp, count);
	}
}

void wil_memcpy_toio_32(volatile void __iomem *dst, const void *src,
			size_t count)
{
	volatile u32 __iomem *d = dst;
	const u32 *s = src;

	for (; count >= 4; count -= 4)
		__raw_writel(*s++, d++);

	if (unlikely(count)) {
		/* count can be 1..3 */
		u32 tmp = 0;

		memcpy(&tmp, s, count);
		__raw_writel(tmp, d);
	}
}

static void wil_disconnect_cid(struct wil6210_priv *wil, int cid,
			       u16 reason_code, bool from_event)
__acquires(&sta->tid_rx_lock) __releases(&sta->tid_rx_lock)
{
	uint i;
	struct net_device *ndev = wil_to_ndev(wil);
	struct wireless_dev *wdev = wil->wdev;
	struct wil_sta_info *sta = &wil->sta[cid];

	might_sleep();
	wil_dbg_misc(wil, "disconnect_cid: CID %d, status %d\n",
		     cid, sta->status);
	/* inform upper/lower layers */
	if (sta->status != wil_sta_unused) {
		if (!from_event) {
			bool del_sta = (wdev->iftype == NL80211_IFTYPE_AP) ?
						disable_ap_sme : false;
			wmi_disconnect_sta(wil, sta->addr, reason_code,
					   true, del_sta);
		}

		switch (wdev->iftype) {
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_P2P_GO:
			/* AP-like interface */
			cfg80211_del_sta(ndev, sta->addr, GFP_KERNEL);
			break;
		default:
			break;
		}
		sta->status = wil_sta_unused;
	}
	/* reorder buffers */
	for (i = 0; i < WIL_STA_TID_NUM; i++) {
		struct wil_tid_ampdu_rx *r;

		spin_lock_bh(&sta->tid_rx_lock);

		r = sta->tid_rx[i];
		sta->tid_rx[i] = NULL;
		wil_tid_ampdu_rx_free(wil, r);

		spin_unlock_bh(&sta->tid_rx_lock);
	}
	/* crypto context */
	memset(sta->tid_crypto_rx, 0, sizeof(sta->tid_crypto_rx));
	memset(&sta->group_crypto_rx, 0, sizeof(sta->group_crypto_rx));
	/* release vrings */
	for (i = 0; i < ARRAY_SIZE(wil->vring_tx); i++) {
		if (wil->vring2cid_tid[i][0] == cid)
			wil_vring_fini_tx(wil, i);
	}
	/* statistics */
	memset(&sta->stats, 0, sizeof(sta->stats));
}

static bool wil_is_connected(struct wil6210_priv *wil)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wil->sta); i++) {
		if (wil->sta[i].status == wil_sta_connected)
			return true;
	}

	return false;
}

static void _wil6210_disconnect(struct wil6210_priv *wil, const u8 *bssid,
				u16 reason_code, bool from_event)
{
	int cid = -ENOENT;
	struct net_device *ndev = wil_to_ndev(wil);
	struct wireless_dev *wdev = wil->wdev;

	if (unlikely(!ndev))
		return;

	might_sleep();
	wil_info(wil, "bssid=%pM, reason=%d, ev%s\n", bssid,
		 reason_code, from_event ? "+" : "-");

	/* Cases are:
	 * - disconnect single STA, still connected
	 * - disconnect single STA, already disconnected
	 * - disconnect all
	 *
	 * For "disconnect all", there are 3 options:
	 * - bssid == NULL
	 * - bssid is broadcast address (ff:ff:ff:ff:ff:ff)
	 * - bssid is our MAC address
	 */
	if (bssid && !is_broadcast_ether_addr(bssid) &&
	    !ether_addr_equal_unaligned(ndev->dev_addr, bssid)) {
		cid = wil_find_cid(wil, bssid);
		wil_dbg_misc(wil, "Disconnect %pM, CID=%d, reason=%d\n",
			     bssid, cid, reason_code);
		if (cid >= 0) /* disconnect 1 peer */
			wil_disconnect_cid(wil, cid, reason_code, from_event);
	} else { /* all */
		wil_dbg_misc(wil, "Disconnect all\n");
		for (cid = 0; cid < WIL6210_MAX_CID; cid++)
			wil_disconnect_cid(wil, cid, reason_code, from_event);
	}

	/* link state */
	switch (wdev->iftype) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		wil_bcast_fini(wil);
		wil_update_net_queues_bh(wil, NULL, true);
		netif_carrier_off(ndev);
		wil6210_bus_request(wil, WIL_DEFAULT_BUS_REQUEST_KBPS);

		if (test_bit(wil_status_fwconnected, wil->status)) {
			clear_bit(wil_status_fwconnected, wil->status);
			cfg80211_disconnected(ndev, reason_code,
					      NULL, 0,
					      wil->locally_generated_disc,
					      GFP_KERNEL);
			wil->locally_generated_disc = false;
		} else if (test_bit(wil_status_fwconnecting, wil->status)) {
			cfg80211_connect_result(ndev, bssid, NULL, 0, NULL, 0,
						WLAN_STATUS_UNSPECIFIED_FAILURE,
						GFP_KERNEL);
			wil->bss = NULL;
		}
		clear_bit(wil_status_fwconnecting, wil->status);
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		if (!wil_is_connected(wil)) {
			wil_update_net_queues_bh(wil, NULL, true);
			clear_bit(wil_status_fwconnected, wil->status);
		} else {
			wil_update_net_queues_bh(wil, NULL, false);
		}
		break;
	default:
		break;
	}
}

static void wil_disconnect_worker(struct work_struct *work)
{
	struct wil6210_priv *wil = container_of(work,
			struct wil6210_priv, disconnect_worker);
	struct net_device *ndev = wil_to_ndev(wil);
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_disconnect_event evt;
	} __packed reply;

	if (test_bit(wil_status_fwconnected, wil->status))
		/* connect succeeded after all */
		return;

	if (!test_bit(wil_status_fwconnecting, wil->status))
		/* already disconnected */
		return;

	rc = wmi_call(wil, WMI_DISCONNECT_CMDID, NULL, 0,
		      WMI_DISCONNECT_EVENTID, &reply, sizeof(reply),
		      WIL6210_DISCONNECT_TO_MS);
	if (rc) {
		wil_err(wil, "disconnect error %d\n", rc);
		return;
	}

	wil_update_net_queues_bh(wil, NULL, true);
	netif_carrier_off(ndev);
	cfg80211_connect_result(ndev, NULL, NULL, 0, NULL, 0,
				WLAN_STATUS_UNSPECIFIED_FAILURE, GFP_KERNEL);
	clear_bit(wil_status_fwconnecting, wil->status);
}

static void wil_connect_timer_fn(ulong x)
{
	struct wil6210_priv *wil = (void *)x;
	bool q;

	wil_err(wil, "Connect timeout detected, disconnect station\n");

	/* reschedule to thread context - disconnect won't
	 * run from atomic context.
	 * queue on wmi_wq to prevent race with connect event.
	 */
	q = queue_work(wil->wmi_wq, &wil->disconnect_worker);
	wil_dbg_wmi(wil, "queue_work of disconnect_worker -> %d\n", q);
}

static void wil_scan_timer_fn(ulong x)
{
	struct wil6210_priv *wil = (void *)x;

	clear_bit(wil_status_fwready, wil->status);
	wil_err(wil, "Scan timeout detected, start fw error recovery\n");
	wil_fw_error_recovery(wil);
}

static int wil_wait_for_recovery(struct wil6210_priv *wil)
{
	if (wait_event_interruptible(wil->wq, wil->recovery_state !=
				     fw_recovery_pending)) {
		wil_err(wil, "Interrupt, canceling recovery\n");
		return -ERESTARTSYS;
	}
	if (wil->recovery_state != fw_recovery_running) {
		wil_info(wil, "Recovery cancelled\n");
		return -EINTR;
	}
	wil_info(wil, "Proceed with recovery\n");
	return 0;
}

void wil_set_recovery_state(struct wil6210_priv *wil, int state)
{
	wil_dbg_misc(wil, "set_recovery_state: %d -> %d\n",
		     wil->recovery_state, state);

	wil->recovery_state = state;
	wake_up_interruptible(&wil->wq);
}

bool wil_is_recovery_blocked(struct wil6210_priv *wil)
{
	return no_fw_recovery && (wil->recovery_state == fw_recovery_pending);
}

static void wil_fw_error_worker(struct work_struct *work)
{
	struct wil6210_priv *wil = container_of(work, struct wil6210_priv,
						fw_error_worker);
	struct wireless_dev *wdev = wil->wdev;
	struct net_device *ndev = wil_to_ndev(wil);

	wil_dbg_misc(wil, "fw error worker\n");

	if (!(ndev->flags & IFF_UP)) {
		wil_info(wil, "No recovery - interface is down\n");
		return;
	}

	/* increment @recovery_count if less then WIL6210_FW_RECOVERY_TO
	 * passed since last recovery attempt
	 */
	if (time_is_after_jiffies(wil->last_fw_recovery +
				  WIL6210_FW_RECOVERY_TO))
		wil->recovery_count++;
	else
		wil->recovery_count = 1; /* fw was alive for a long time */

	if (wil->recovery_count > WIL6210_FW_RECOVERY_RETRIES) {
		wil_err(wil, "too many recovery attempts (%d), giving up\n",
			wil->recovery_count);
		return;
	}

	wil->last_fw_recovery = jiffies;

	wil_info(wil, "fw error recovery requested (try %d)...\n",
		 wil->recovery_count);
	if (!no_fw_recovery)
		wil->recovery_state = fw_recovery_running;
	if (wil_wait_for_recovery(wil) != 0)
		return;

	mutex_lock(&wil->mutex);
	switch (wdev->iftype) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_MONITOR:
		/* silent recovery, upper layers will see disconnect */
		__wil_down(wil);
		__wil_up(wil);
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		wil_info(wil, "No recovery for AP-like interface\n");
		/* recovery in these modes is done by upper layers */
		break;
	default:
		wil_err(wil, "No recovery - unknown interface type %d\n",
			wdev->iftype);
		break;
	}
	mutex_unlock(&wil->mutex);
}

static int wil_find_free_vring(struct wil6210_priv *wil)
{
	int i;

	for (i = 0; i < WIL6210_MAX_TX_RINGS; i++) {
		if (!wil->vring_tx[i].va)
			return i;
	}
	return -EINVAL;
}

int wil_tx_init(struct wil6210_priv *wil, int cid)
{
	int rc = -EINVAL, ringid;

	if (cid < 0) {
		wil_err(wil, "No connection pending\n");
		goto out;
	}
	ringid = wil_find_free_vring(wil);
	if (ringid < 0) {
		wil_err(wil, "No free vring found\n");
		goto out;
	}

	wil_dbg_wmi(wil, "Configure for connection CID %d vring %d\n",
		    cid, ringid);

	rc = wil_vring_init_tx(wil, ringid, 1 << tx_ring_order, cid, 0);
	if (rc)
		wil_err(wil, "wil_vring_init_tx for CID %d vring %d failed\n",
			cid, ringid);

out:
	return rc;
}

int wil_bcast_init(struct wil6210_priv *wil)
{
	int ri = wil->bcast_vring, rc;

	if ((ri >= 0) && wil->vring_tx[ri].va)
		return 0;

	ri = wil_find_free_vring(wil);
	if (ri < 0)
		return ri;

	wil->bcast_vring = ri;
	rc = wil_vring_init_bcast(wil, ri, 1 << bcast_ring_order);
	if (rc)
		wil->bcast_vring = -1;

	return rc;
}

void wil_bcast_fini(struct wil6210_priv *wil)
{
	int ri = wil->bcast_vring;

	if (ri < 0)
		return;

	wil->bcast_vring = -1;
	wil_vring_fini_tx(wil, ri);
}

int wil_priv_init(struct wil6210_priv *wil)
{
	uint i;

	wil_dbg_misc(wil, "priv_init\n");

	memset(wil->sta, 0, sizeof(wil->sta));
	for (i = 0; i < WIL6210_MAX_CID; i++)
		spin_lock_init(&wil->sta[i].tid_rx_lock);

	for (i = 0; i < WIL6210_MAX_TX_RINGS; i++)
		spin_lock_init(&wil->vring_tx_data[i].lock);

	mutex_init(&wil->mutex);
	mutex_init(&wil->wmi_mutex);
	mutex_init(&wil->probe_client_mutex);
	mutex_init(&wil->p2p_wdev_mutex);
	mutex_init(&wil->halp.lock);

	init_completion(&wil->wmi_ready);
	init_completion(&wil->wmi_call);
	init_completion(&wil->halp.comp);

	wil->bcast_vring = -1;
	setup_timer(&wil->connect_timer, wil_connect_timer_fn, (ulong)wil);
	setup_timer(&wil->scan_timer, wil_scan_timer_fn, (ulong)wil);
	setup_timer(&wil->p2p.discovery_timer, wil_p2p_discovery_timer_fn,
		    (ulong)wil);

	INIT_WORK(&wil->disconnect_worker, wil_disconnect_worker);
	INIT_WORK(&wil->wmi_event_worker, wmi_event_worker);
	INIT_WORK(&wil->fw_error_worker, wil_fw_error_worker);
	INIT_WORK(&wil->probe_client_worker, wil_probe_client_worker);
	INIT_WORK(&wil->p2p.delayed_listen_work, wil_p2p_delayed_listen_work);

	INIT_LIST_HEAD(&wil->pending_wmi_ev);
	INIT_LIST_HEAD(&wil->probe_client_pending);
	spin_lock_init(&wil->wmi_ev_lock);
	spin_lock_init(&wil->net_queue_lock);
	wil->net_queue_stopped = 1;
	init_waitqueue_head(&wil->wq);

	wil->wmi_wq = create_singlethread_workqueue(WIL_NAME "_wmi");
	if (!wil->wmi_wq)
		return -EAGAIN;

	wil->wq_service = create_singlethread_workqueue(WIL_NAME "_service");
	if (!wil->wq_service)
		goto out_wmi_wq;

	wil->last_fw_recovery = jiffies;
	wil->tx_interframe_timeout = WIL6210_ITR_TX_INTERFRAME_TIMEOUT_DEFAULT;
	wil->rx_interframe_timeout = WIL6210_ITR_RX_INTERFRAME_TIMEOUT_DEFAULT;
	wil->tx_max_burst_duration = WIL6210_ITR_TX_MAX_BURST_DURATION_DEFAULT;
	wil->rx_max_burst_duration = WIL6210_ITR_RX_MAX_BURST_DURATION_DEFAULT;

	if (rx_ring_overflow_thrsh == WIL6210_RX_HIGH_TRSH_INIT)
		rx_ring_overflow_thrsh = WIL6210_RX_HIGH_TRSH_DEFAULT;

	wil->ps_profile =  WMI_PS_PROFILE_TYPE_DEFAULT;

	wil->wakeup_trigger = WMI_WAKEUP_TRIGGER_UCAST |
			      WMI_WAKEUP_TRIGGER_BCAST;
	memset(&wil->suspend_stats, 0, sizeof(wil->suspend_stats));
	wil->suspend_stats.min_suspend_time = ULONG_MAX;
	wil->vring_idle_trsh = 16;

	return 0;

out_wmi_wq:
	destroy_workqueue(wil->wmi_wq);

	return -EAGAIN;
}

void wil6210_bus_request(struct wil6210_priv *wil, u32 kbps)
{
	if (wil->platform_ops.bus_request) {
		wil->bus_request_kbps = kbps;
		wil->platform_ops.bus_request(wil->platform_handle, kbps);
	}
}

/**
 * wil6210_disconnect - disconnect one connection
 * @wil: driver context
 * @bssid: peer to disconnect, NULL to disconnect all
 * @reason_code: Reason code for the Disassociation frame
 * @from_event: whether is invoked from FW event handler
 *
 * Disconnect and release associated resources. If invoked not from the
 * FW event handler, issue WMI command(s) to trigger MAC disconnect.
 */
void wil6210_disconnect(struct wil6210_priv *wil, const u8 *bssid,
			u16 reason_code, bool from_event)
{
	wil_dbg_misc(wil, "disconnect\n");

	del_timer_sync(&wil->connect_timer);
	_wil6210_disconnect(wil, bssid, reason_code, from_event);
}

void wil_priv_deinit(struct wil6210_priv *wil)
{
	wil_dbg_misc(wil, "priv_deinit\n");

	wil_set_recovery_state(wil, fw_recovery_idle);
	del_timer_sync(&wil->scan_timer);
	del_timer_sync(&wil->p2p.discovery_timer);
	cancel_work_sync(&wil->disconnect_worker);
	cancel_work_sync(&wil->fw_error_worker);
	cancel_work_sync(&wil->p2p.discovery_expired_work);
	cancel_work_sync(&wil->p2p.delayed_listen_work);
	mutex_lock(&wil->mutex);
	wil6210_disconnect(wil, NULL, WLAN_REASON_DEAUTH_LEAVING, false);
	mutex_unlock(&wil->mutex);
	wmi_event_flush(wil);
	wil_probe_client_flush(wil);
	cancel_work_sync(&wil->probe_client_worker);
	destroy_workqueue(wil->wq_service);
	destroy_workqueue(wil->wmi_wq);
}

static inline void wil_halt_cpu(struct wil6210_priv *wil)
{
	wil_w(wil, RGF_USER_USER_CPU_0, BIT_USER_USER_CPU_MAN_RST);
	wil_w(wil, RGF_USER_MAC_CPU_0,  BIT_USER_MAC_CPU_MAN_RST);
}

static inline void wil_release_cpu(struct wil6210_priv *wil)
{
	/* Start CPU */
	wil_w(wil, RGF_USER_USER_CPU_0, 1);
}

static void wil_set_oob_mode(struct wil6210_priv *wil, u8 mode)
{
	wil_info(wil, "oob_mode to %d\n", mode);
	switch (mode) {
	case 0:
		wil_c(wil, RGF_USER_USAGE_6, BIT_USER_OOB_MODE |
		      BIT_USER_OOB_R2_MODE);
		break;
	case 1:
		wil_c(wil, RGF_USER_USAGE_6, BIT_USER_OOB_R2_MODE);
		wil_s(wil, RGF_USER_USAGE_6, BIT_USER_OOB_MODE);
		break;
	case 2:
		wil_c(wil, RGF_USER_USAGE_6, BIT_USER_OOB_MODE);
		wil_s(wil, RGF_USER_USAGE_6, BIT_USER_OOB_R2_MODE);
		break;
	default:
		wil_err(wil, "invalid oob_mode: %d\n", mode);
	}
}

static int wil_target_reset(struct wil6210_priv *wil)
{
	int delay = 0;
	u32 x, x1 = 0;

	wil_dbg_misc(wil, "Resetting \"%s\"...\n", wil->hw_name);

	/* Clear MAC link up */
	wil_s(wil, RGF_HP_CTRL, BIT(15));
	wil_s(wil, RGF_USER_CLKS_CTL_SW_RST_MASK_0, BIT_HPAL_PERST_FROM_PAD);
	wil_s(wil, RGF_USER_CLKS_CTL_SW_RST_MASK_0, BIT_CAR_PERST_RST);

	wil_halt_cpu(wil);

	/* clear all boot loader "ready" bits */
	wil_w(wil, RGF_USER_BL +
	      offsetof(struct bl_dedicated_registers_v0, boot_loader_ready), 0);
	/* Clear Fw Download notification */
	wil_c(wil, RGF_USER_USAGE_6, BIT(0));

	wil_s(wil, RGF_CAF_OSC_CONTROL, BIT_CAF_OSC_XTAL_EN);
	/* XTAL stabilization should take about 3ms */
	usleep_range(5000, 7000);
	x = wil_r(wil, RGF_CAF_PLL_LOCK_STATUS);
	if (!(x & BIT_CAF_OSC_DIG_XTAL_STABLE)) {
		wil_err(wil, "Xtal stabilization timeout\n"
			"RGF_CAF_PLL_LOCK_STATUS = 0x%08x\n", x);
		return -ETIME;
	}
	/* switch 10k to XTAL*/
	wil_c(wil, RGF_USER_SPARROW_M_4, BIT_SPARROW_M_4_SEL_SLEEP_OR_REF);
	/* 40 MHz */
	wil_c(wil, RGF_USER_CLKS_CTL_0, BIT_USER_CLKS_CAR_AHB_SW_SEL);

	wil_w(wil, RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_0, 0x3ff81f);
	wil_w(wil, RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_1, 0xf);

	wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0xFE000000);
	wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_1, 0x0000003F);
	wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_3, 0x000000f0);
	wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_0, 0xFFE7FE00);

	wil_w(wil, RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_0, 0x0);
	wil_w(wil, RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_1, 0x0);

	wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_3, 0);
	wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0);
	wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_1, 0);
	wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_0, 0);

	wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_3, 0x00000003);
	/* reset A2 PCIE AHB */
	wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0x00008000);

	wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_0, 0);

	/* wait until device ready. typical time is 20..80 msec */
	do {
		msleep(RST_DELAY);
		x = wil_r(wil, RGF_USER_BL +
			  offsetof(struct bl_dedicated_registers_v0,
				   boot_loader_ready));
		if (x1 != x) {
			wil_dbg_misc(wil, "BL.ready 0x%08x => 0x%08x\n", x1, x);
			x1 = x;
		}
		if (delay++ > RST_COUNT) {
			wil_err(wil, "Reset not completed, bl.ready 0x%08x\n",
				x);
			return -ETIME;
		}
	} while (x != BL_READY);

	wil_c(wil, RGF_USER_CLKS_CTL_0, BIT_USER_CLKS_RST_PWGD);

	/* enable fix for HW bug related to the SA/DA swap in AP Rx */
	wil_s(wil, RGF_DMA_OFUL_NID_0, BIT_DMA_OFUL_NID_0_RX_EXT_TR_EN |
	      BIT_DMA_OFUL_NID_0_RX_EXT_A3_SRC);

	wil_dbg_misc(wil, "Reset completed in %d ms\n", delay * RST_DELAY);
	return 0;
}

static void wil_collect_fw_info(struct wil6210_priv *wil)
{
	struct wiphy *wiphy = wil_to_wiphy(wil);
	u8 retry_short;
	int rc;

	rc = wmi_get_mgmt_retry(wil, &retry_short);
	if (!rc) {
		wiphy->retry_short = retry_short;
		wil_dbg_misc(wil, "FW retry_short: %d\n", retry_short);
	}
}

void wil_mbox_ring_le2cpus(struct wil6210_mbox_ring *r)
{
	le32_to_cpus(&r->base);
	le16_to_cpus(&r->entry_size);
	le16_to_cpus(&r->size);
	le32_to_cpus(&r->tail);
	le32_to_cpus(&r->head);
}

static int wil_get_bl_info(struct wil6210_priv *wil)
{
	struct net_device *ndev = wil_to_ndev(wil);
	struct wiphy *wiphy = wil_to_wiphy(wil);
	union {
		struct bl_dedicated_registers_v0 bl0;
		struct bl_dedicated_registers_v1 bl1;
	} bl;
	u32 bl_ver;
	u8 *mac;
	u16 rf_status;

	wil_memcpy_fromio_32(&bl, wil->csr + HOSTADDR(RGF_USER_BL),
			     sizeof(bl));
	bl_ver = le32_to_cpu(bl.bl0.boot_loader_struct_version);
	mac = bl.bl0.mac_address;

	if (bl_ver == 0) {
		le32_to_cpus(&bl.bl0.rf_type);
		le32_to_cpus(&bl.bl0.baseband_type);
		rf_status = 0; /* actually, unknown */
		wil_info(wil,
			 "Boot Loader struct v%d: MAC = %pM RF = 0x%08x bband = 0x%08x\n",
			 bl_ver, mac,
			 bl.bl0.rf_type, bl.bl0.baseband_type);
		wil_info(wil, "Boot Loader build unknown for struct v0\n");
	} else {
		le16_to_cpus(&bl.bl1.rf_type);
		rf_status = le16_to_cpu(bl.bl1.rf_status);
		le32_to_cpus(&bl.bl1.baseband_type);
		le16_to_cpus(&bl.bl1.bl_version_subminor);
		le16_to_cpus(&bl.bl1.bl_version_build);
		wil_info(wil,
			 "Boot Loader struct v%d: MAC = %pM RF = 0x%04x (status 0x%04x) bband = 0x%08x\n",
			 bl_ver, mac,
			 bl.bl1.rf_type, rf_status,
			 bl.bl1.baseband_type);
		wil_info(wil, "Boot Loader build %d.%d.%d.%d\n",
			 bl.bl1.bl_version_major, bl.bl1.bl_version_minor,
			 bl.bl1.bl_version_subminor, bl.bl1.bl_version_build);
	}

	if (!is_valid_ether_addr(mac)) {
		wil_err(wil, "BL: Invalid MAC %pM\n", mac);
		return -EINVAL;
	}

	ether_addr_copy(ndev->perm_addr, mac);
	ether_addr_copy(wiphy->perm_addr, mac);
	if (!is_valid_ether_addr(ndev->dev_addr))
		ether_addr_copy(ndev->dev_addr, mac);

	if (rf_status) {/* bad RF cable? */
		wil_err(wil, "RF communication error 0x%04x",
			rf_status);
		return -EAGAIN;
	}

	return 0;
}

static void wil_bl_crash_info(struct wil6210_priv *wil, bool is_err)
{
	u32 bl_assert_code, bl_assert_blink, bl_magic_number;
	u32 bl_ver = wil_r(wil, RGF_USER_BL +
			   offsetof(struct bl_dedicated_registers_v0,
				    boot_loader_struct_version));

	if (bl_ver < 2)
		return;

	bl_assert_code = wil_r(wil, RGF_USER_BL +
			       offsetof(struct bl_dedicated_registers_v1,
					bl_assert_code));
	bl_assert_blink = wil_r(wil, RGF_USER_BL +
				offsetof(struct bl_dedicated_registers_v1,
					 bl_assert_blink));
	bl_magic_number = wil_r(wil, RGF_USER_BL +
				offsetof(struct bl_dedicated_registers_v1,
					 bl_magic_number));

	if (is_err) {
		wil_err(wil,
			"BL assert code 0x%08x blink 0x%08x magic 0x%08x\n",
			bl_assert_code, bl_assert_blink, bl_magic_number);
	} else {
		wil_dbg_misc(wil,
			     "BL assert code 0x%08x blink 0x%08x magic 0x%08x\n",
			     bl_assert_code, bl_assert_blink, bl_magic_number);
	}
}

static int wil_wait_for_fw_ready(struct wil6210_priv *wil)
{
	ulong to = msecs_to_jiffies(1000);
	ulong left = wait_for_completion_timeout(&wil->wmi_ready, to);

	if (0 == left) {
		wil_err(wil, "Firmware not ready\n");
		return -ETIME;
	} else {
		wil_info(wil, "FW ready after %d ms. HW version 0x%08x\n",
			 jiffies_to_msecs(to-left), wil->hw_version);
	}
	return 0;
}

void wil_abort_scan(struct wil6210_priv *wil, bool sync)
{
	int rc;
	struct cfg80211_scan_info info = {
		.aborted = true,
	};

	lockdep_assert_held(&wil->p2p_wdev_mutex);

	if (!wil->scan_request)
		return;

	wil_dbg_misc(wil, "Abort scan_request 0x%p\n", wil->scan_request);
	del_timer_sync(&wil->scan_timer);
	mutex_unlock(&wil->p2p_wdev_mutex);
	rc = wmi_abort_scan(wil);
	if (!rc && sync)
		wait_event_interruptible_timeout(wil->wq, !wil->scan_request,
						 msecs_to_jiffies(
						 WAIT_FOR_SCAN_ABORT_MS));

	mutex_lock(&wil->p2p_wdev_mutex);
	if (wil->scan_request) {
		cfg80211_scan_done(wil->scan_request, &info);
		wil->scan_request = NULL;
	}
}

int wil_ps_update(struct wil6210_priv *wil, enum wmi_ps_profile_type ps_profile)
{
	int rc;

	if (!test_bit(WMI_FW_CAPABILITY_PS_CONFIG, wil->fw_capabilities)) {
		wil_err(wil, "set_power_mgmt not supported\n");
		return -EOPNOTSUPP;
	}

	rc  = wmi_ps_dev_profile_cfg(wil, ps_profile);
	if (rc)
		wil_err(wil, "wmi_ps_dev_profile_cfg failed (%d)\n", rc);
	else
		wil->ps_profile = ps_profile;

	return rc;
}

static void wil_pre_fw_config(struct wil6210_priv *wil)
{
	/* Mark FW as loaded from host */
	wil_s(wil, RGF_USER_USAGE_6, 1);

	/* clear any interrupts which on-card-firmware
	 * may have set
	 */
	wil6210_clear_irq(wil);
	/* CAF_ICR - clear and mask */
	/* it is W1C, clear by writing back same value */
	wil_s(wil, RGF_CAF_ICR + offsetof(struct RGF_ICR, ICR), 0);
	wil_w(wil, RGF_CAF_ICR + offsetof(struct RGF_ICR, IMV), ~0);
	/* clear PAL_UNIT_ICR (potential D0->D3 leftover) */
	wil_s(wil, RGF_PAL_UNIT_ICR + offsetof(struct RGF_ICR, ICR), 0);

	if (wil->fw_calib_result > 0) {
		__le32 val = cpu_to_le32(wil->fw_calib_result |
						(CALIB_RESULT_SIGNATURE << 8));
		wil_w(wil, RGF_USER_FW_CALIB_RESULT, (u32 __force)val);
	}
}

/*
 * We reset all the structures, and we reset the UMAC.
 * After calling this routine, you're expected to reload
 * the firmware.
 */
int wil_reset(struct wil6210_priv *wil, bool load_fw)
{
	int rc;

	wil_dbg_misc(wil, "reset\n");

	WARN_ON(!mutex_is_locked(&wil->mutex));
	WARN_ON(test_bit(wil_status_napi_en, wil->status));

	if (debug_fw) {
		static const u8 mac[ETH_ALEN] = {
			0x00, 0xde, 0xad, 0x12, 0x34, 0x56,
		};
		struct net_device *ndev = wil_to_ndev(wil);

		ether_addr_copy(ndev->perm_addr, mac);
		ether_addr_copy(ndev->dev_addr, ndev->perm_addr);
		return 0;
	}

	if (wil->hw_version == HW_VER_UNKNOWN)
		return -ENODEV;

	if (wil->platform_ops.notify) {
		rc = wil->platform_ops.notify(wil->platform_handle,
					      WIL_PLATFORM_EVT_PRE_RESET);
		if (rc)
			wil_err(wil, "PRE_RESET platform notify failed, rc %d\n",
				rc);
	}

	set_bit(wil_status_resetting, wil->status);

	cancel_work_sync(&wil->disconnect_worker);
	wil6210_disconnect(wil, NULL, WLAN_REASON_DEAUTH_LEAVING, false);
	wil_bcast_fini(wil);

	/* Disable device led before reset*/
	wmi_led_cfg(wil, false);

	mutex_lock(&wil->p2p_wdev_mutex);
	wil_abort_scan(wil, false);
	mutex_unlock(&wil->p2p_wdev_mutex);

	/* prevent NAPI from being scheduled and prevent wmi commands */
	mutex_lock(&wil->wmi_mutex);
	bitmap_zero(wil->status, wil_status_last);
	mutex_unlock(&wil->wmi_mutex);

	wil_mask_irq(wil);

	wmi_event_flush(wil);

	flush_workqueue(wil->wq_service);
	flush_workqueue(wil->wmi_wq);

	wil_bl_crash_info(wil, false);
	wil_disable_irq(wil);
	rc = wil_target_reset(wil);
	wil6210_clear_irq(wil);
	wil_enable_irq(wil);
	wil_rx_fini(wil);
	if (rc) {
		wil_bl_crash_info(wil, true);
		return rc;
	}

	rc = wil_get_bl_info(wil);
	if (rc == -EAGAIN && !load_fw) /* ignore RF error if not going up */
		rc = 0;
	if (rc)
		return rc;

	wil_set_oob_mode(wil, oob_mode);
	if (load_fw) {
		wil_info(wil, "Use firmware <%s> + board <%s>\n",
			 wil->wil_fw_name, WIL_BOARD_FILE_NAME);

		wil_halt_cpu(wil);
		memset(wil->fw_version, 0, sizeof(wil->fw_version));
		/* Loading f/w from the file */
		rc = wil_request_firmware(wil, wil->wil_fw_name, true);
		if (rc)
			return rc;
		rc = wil_request_firmware(wil, WIL_BOARD_FILE_NAME, true);
		if (rc)
			return rc;

		wil_pre_fw_config(wil);
		wil_release_cpu(wil);
	}

	/* init after reset */
	wil->ap_isolate = 0;
	reinit_completion(&wil->wmi_ready);
	reinit_completion(&wil->wmi_call);
	reinit_completion(&wil->halp.comp);

	if (load_fw) {
		wil_configure_interrupt_moderation(wil);
		wil_unmask_irq(wil);

		/* we just started MAC, wait for FW ready */
		rc = wil_wait_for_fw_ready(wil);
		if (rc)
			return rc;

		/* check FW is responsive */
		rc = wmi_echo(wil);
		if (rc) {
			wil_err(wil, "wmi_echo failed, rc %d\n", rc);
			return rc;
		}

		if (wil->ps_profile != WMI_PS_PROFILE_TYPE_DEFAULT)
			wil_ps_update(wil, wil->ps_profile);

		wil_collect_fw_info(wil);

		if (wil->platform_ops.notify) {
			rc = wil->platform_ops.notify(wil->platform_handle,
						      WIL_PLATFORM_EVT_FW_RDY);
			if (rc) {
				wil_err(wil, "FW_RDY notify failed, rc %d\n",
					rc);
				rc = 0;
			}
		}
	}

	return rc;
}

void wil_fw_error_recovery(struct wil6210_priv *wil)
{
	wil_dbg_misc(wil, "starting fw error recovery\n");

	if (test_bit(wil_status_resetting, wil->status)) {
		wil_info(wil, "Reset already in progress\n");
		return;
	}

	wil->recovery_state = fw_recovery_pending;
	schedule_work(&wil->fw_error_worker);
}

int __wil_up(struct wil6210_priv *wil)
{
	struct net_device *ndev = wil_to_ndev(wil);
	struct wireless_dev *wdev = wil->wdev;
	int rc;

	WARN_ON(!mutex_is_locked(&wil->mutex));

	rc = wil_reset(wil, true);
	if (rc)
		return rc;

	/* Rx VRING. After MAC and beacon */
	rc = wil_rx_init(wil, 1 << rx_ring_order);
	if (rc)
		return rc;

	switch (wdev->iftype) {
	case NL80211_IFTYPE_STATION:
		wil_dbg_misc(wil, "type: STATION\n");
		ndev->type = ARPHRD_ETHER;
		break;
	case NL80211_IFTYPE_AP:
		wil_dbg_misc(wil, "type: AP\n");
		ndev->type = ARPHRD_ETHER;
		break;
	case NL80211_IFTYPE_P2P_CLIENT:
		wil_dbg_misc(wil, "type: P2P_CLIENT\n");
		ndev->type = ARPHRD_ETHER;
		break;
	case NL80211_IFTYPE_P2P_GO:
		wil_dbg_misc(wil, "type: P2P_GO\n");
		ndev->type = ARPHRD_ETHER;
		break;
	case NL80211_IFTYPE_MONITOR:
		wil_dbg_misc(wil, "type: Monitor\n");
		ndev->type = ARPHRD_IEEE80211_RADIOTAP;
		/* ARPHRD_IEEE80211 or ARPHRD_IEEE80211_RADIOTAP ? */
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* MAC address - pre-requisite for other commands */
	wmi_set_mac_address(wil, ndev->dev_addr);

	wil_dbg_misc(wil, "NAPI enable\n");
	napi_enable(&wil->napi_rx);
	napi_enable(&wil->napi_tx);
	set_bit(wil_status_napi_en, wil->status);

	wil6210_bus_request(wil, WIL_DEFAULT_BUS_REQUEST_KBPS);

	return 0;
}

int wil_up(struct wil6210_priv *wil)
{
	int rc;

	wil_dbg_misc(wil, "up\n");

	mutex_lock(&wil->mutex);
	rc = __wil_up(wil);
	mutex_unlock(&wil->mutex);

	return rc;
}

int __wil_down(struct wil6210_priv *wil)
{
	WARN_ON(!mutex_is_locked(&wil->mutex));

	set_bit(wil_status_resetting, wil->status);

	wil6210_bus_request(wil, 0);

	wil_disable_irq(wil);
	if (test_and_clear_bit(wil_status_napi_en, wil->status)) {
		napi_disable(&wil->napi_rx);
		napi_disable(&wil->napi_tx);
		wil_dbg_misc(wil, "NAPI disable\n");
	}
	wil_enable_irq(wil);

	mutex_lock(&wil->p2p_wdev_mutex);
	wil_p2p_stop_radio_operations(wil);
	wil_abort_scan(wil, false);
	mutex_unlock(&wil->p2p_wdev_mutex);

	wil_reset(wil, false);

	return 0;
}

int wil_down(struct wil6210_priv *wil)
{
	int rc;

	wil_dbg_misc(wil, "down\n");

	wil_set_recovery_state(wil, fw_recovery_idle);
	mutex_lock(&wil->mutex);
	rc = __wil_down(wil);
	mutex_unlock(&wil->mutex);

	return rc;
}

int wil_find_cid(struct wil6210_priv *wil, const u8 *mac)
{
	int i;
	int rc = -ENOENT;

	for (i = 0; i < ARRAY_SIZE(wil->sta); i++) {
		if ((wil->sta[i].status != wil_sta_unused) &&
		    ether_addr_equal(wil->sta[i].addr, mac)) {
			rc = i;
			break;
		}
	}

	return rc;
}

void wil_halp_vote(struct wil6210_priv *wil)
{
	unsigned long rc;
	unsigned long to_jiffies = msecs_to_jiffies(WAIT_FOR_HALP_VOTE_MS);

	mutex_lock(&wil->halp.lock);

	wil_dbg_irq(wil, "halp_vote: start, HALP ref_cnt (%d)\n",
		    wil->halp.ref_cnt);

	if (++wil->halp.ref_cnt == 1) {
		reinit_completion(&wil->halp.comp);
		wil6210_set_halp(wil);
		rc = wait_for_completion_timeout(&wil->halp.comp, to_jiffies);
		if (!rc) {
			wil_err(wil, "HALP vote timed out\n");
			/* Mask HALP as done in case the interrupt is raised */
			wil6210_mask_halp(wil);
		} else {
			wil_dbg_irq(wil,
				    "halp_vote: HALP vote completed after %d ms\n",
				    jiffies_to_msecs(to_jiffies - rc));
		}
	}

	wil_dbg_irq(wil, "halp_vote: end, HALP ref_cnt (%d)\n",
		    wil->halp.ref_cnt);

	mutex_unlock(&wil->halp.lock);
}

void wil_halp_unvote(struct wil6210_priv *wil)
{
	WARN_ON(wil->halp.ref_cnt == 0);

	mutex_lock(&wil->halp.lock);

	wil_dbg_irq(wil, "halp_unvote: start, HALP ref_cnt (%d)\n",
		    wil->halp.ref_cnt);

	if (--wil->halp.ref_cnt == 0) {
		wil6210_clear_halp(wil);
		wil_dbg_irq(wil, "HALP unvote\n");
	}

	wil_dbg_irq(wil, "halp_unvote:end, HALP ref_cnt (%d)\n",
		    wil->halp.ref_cnt);

	mutex_unlock(&wil->halp.lock);
}
