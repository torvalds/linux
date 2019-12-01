/*
 * Copyright (c) 2012-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
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
#include "txrx_edma.h"
#include "wmi.h"
#include "boot_loader.h"

#define WAIT_FOR_HALP_VOTE_MS 100
#define WAIT_FOR_SCAN_ABORT_MS 1000
#define WIL_DEFAULT_NUM_RX_STATUS_RINGS 1
#define WIL_BOARD_FILE_MAX_NAMELEN 128

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

enum {
	WIL_BOOT_ERR,
	WIL_BOOT_VANILLA,
	WIL_BOOT_PRODUCTION,
	WIL_BOOT_DEVELOPMENT,
};

enum {
	WIL_SIG_STATUS_VANILLA = 0x0,
	WIL_SIG_STATUS_DEVELOPMENT = 0x1,
	WIL_SIG_STATUS_PRODUCTION = 0x2,
	WIL_SIG_STATUS_CORRUPTED_PRODUCTION = 0x3,
};

#define RST_DELAY (20) /* msec, for loop in @wil_wait_device_ready */
#define RST_COUNT (1 + 1000/RST_DELAY) /* round up to be above 1 sec total */

#define PMU_READY_DELAY_MS (4) /* ms, for sleep in @wil_wait_device_ready */

#define OTP_HW_DELAY (200) /* usec, loop in @wil_wait_device_ready_talyn_mb */
/* round up to be above 2 ms total */
#define OTP_HW_COUNT (1 + 2000 / OTP_HW_DELAY)

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

static void wil_ring_fini_tx(struct wil6210_priv *wil, int id)
{
	struct wil_ring *ring = &wil->ring_tx[id];
	struct wil_ring_tx_data *txdata = &wil->ring_tx_data[id];

	lockdep_assert_held(&wil->mutex);

	if (!ring->va)
		return;

	wil_dbg_misc(wil, "vring_fini_tx: id=%d\n", id);

	spin_lock_bh(&txdata->lock);
	txdata->dot1x_open = false;
	txdata->mid = U8_MAX;
	txdata->enabled = 0; /* no Tx can be in progress or start anew */
	spin_unlock_bh(&txdata->lock);
	/* napi_synchronize waits for completion of the current NAPI but will
	 * not prevent the next NAPI run.
	 * Add a memory barrier to guarantee that txdata->enabled is zeroed
	 * before napi_synchronize so that the next scheduled NAPI will not
	 * handle this vring
	 */
	wmb();
	/* make sure NAPI won't touch this vring */
	if (test_bit(wil_status_napi_en, wil->status))
		napi_synchronize(&wil->napi_tx);

	wil->txrx_ops.ring_fini_tx(wil, ring);
}

static void wil_disconnect_cid(struct wil6210_vif *vif, int cid,
			       u16 reason_code, bool from_event)
__acquires(&sta->tid_rx_lock) __releases(&sta->tid_rx_lock)
{
	uint i;
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct net_device *ndev = vif_to_ndev(vif);
	struct wireless_dev *wdev = vif_to_wdev(vif);
	struct wil_sta_info *sta = &wil->sta[cid];
	int min_ring_id = wil_get_min_tx_ring_id(wil);

	might_sleep();
	wil_dbg_misc(wil, "disconnect_cid: CID %d, MID %d, status %d\n",
		     cid, sta->mid, sta->status);
	/* inform upper/lower layers */
	if (sta->status != wil_sta_unused) {
		if (vif->mid != sta->mid) {
			wil_err(wil, "STA MID mismatch with VIF MID(%d)\n",
				vif->mid);
			/* let FW override sta->mid but be more strict with
			 * user space requests
			 */
			if (!from_event)
				return;
		}
		if (!from_event) {
			bool del_sta = (wdev->iftype == NL80211_IFTYPE_AP) ?
						disable_ap_sme : false;
			wmi_disconnect_sta(vif, sta->addr, reason_code,
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
		sta->mid = U8_MAX;
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
	for (i = min_ring_id; i < ARRAY_SIZE(wil->ring_tx); i++) {
		if (wil->ring2cid_tid[i][0] == cid)
			wil_ring_fini_tx(wil, i);
	}
	/* statistics */
	memset(&sta->stats, 0, sizeof(sta->stats));
	sta->stats.tx_latency_min_us = U32_MAX;
}

static bool wil_vif_is_connected(struct wil6210_priv *wil, u8 mid)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wil->sta); i++) {
		if (wil->sta[i].mid == mid &&
		    wil->sta[i].status == wil_sta_connected)
			return true;
	}

	return false;
}

static void _wil6210_disconnect(struct wil6210_vif *vif, const u8 *bssid,
				u16 reason_code, bool from_event)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int cid = -ENOENT;
	struct net_device *ndev;
	struct wireless_dev *wdev;

	if (unlikely(!vif))
		return;

	ndev = vif_to_ndev(vif);
	wdev = vif_to_wdev(vif);

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
		cid = wil_find_cid(wil, vif->mid, bssid);
		wil_dbg_misc(wil, "Disconnect %pM, CID=%d, reason=%d\n",
			     bssid, cid, reason_code);
		if (cid >= 0) /* disconnect 1 peer */
			wil_disconnect_cid(vif, cid, reason_code, from_event);
	} else { /* all */
		wil_dbg_misc(wil, "Disconnect all\n");
		for (cid = 0; cid < WIL6210_MAX_CID; cid++)
			wil_disconnect_cid(vif, cid, reason_code, from_event);
	}

	/* link state */
	switch (wdev->iftype) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		wil_bcast_fini(vif);
		wil_update_net_queues_bh(wil, vif, NULL, true);
		netif_carrier_off(ndev);
		if (!wil_has_other_active_ifaces(wil, ndev, false, true))
			wil6210_bus_request(wil, WIL_DEFAULT_BUS_REQUEST_KBPS);

		if (test_and_clear_bit(wil_vif_fwconnected, vif->status)) {
			atomic_dec(&wil->connected_vifs);
			cfg80211_disconnected(ndev, reason_code,
					      NULL, 0,
					      vif->locally_generated_disc,
					      GFP_KERNEL);
			vif->locally_generated_disc = false;
		} else if (test_bit(wil_vif_fwconnecting, vif->status)) {
			cfg80211_connect_result(ndev, bssid, NULL, 0, NULL, 0,
						WLAN_STATUS_UNSPECIFIED_FAILURE,
						GFP_KERNEL);
			vif->bss = NULL;
		}
		clear_bit(wil_vif_fwconnecting, vif->status);
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		if (!wil_vif_is_connected(wil, vif->mid)) {
			wil_update_net_queues_bh(wil, vif, NULL, true);
			if (test_and_clear_bit(wil_vif_fwconnected,
					       vif->status))
				atomic_dec(&wil->connected_vifs);
		} else {
			wil_update_net_queues_bh(wil, vif, NULL, false);
		}
		break;
	default:
		break;
	}
}

void wil_disconnect_worker(struct work_struct *work)
{
	struct wil6210_vif *vif = container_of(work,
			struct wil6210_vif, disconnect_worker);
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct net_device *ndev = vif_to_ndev(vif);
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_disconnect_event evt;
	} __packed reply;

	if (test_bit(wil_vif_fwconnected, vif->status))
		/* connect succeeded after all */
		return;

	if (!test_bit(wil_vif_fwconnecting, vif->status))
		/* already disconnected */
		return;

	memset(&reply, 0, sizeof(reply));

	rc = wmi_call(wil, WMI_DISCONNECT_CMDID, vif->mid, NULL, 0,
		      WMI_DISCONNECT_EVENTID, &reply, sizeof(reply),
		      WIL6210_DISCONNECT_TO_MS);
	if (rc) {
		wil_err(wil, "disconnect error %d\n", rc);
		return;
	}

	wil_update_net_queues_bh(wil, vif, NULL, true);
	netif_carrier_off(ndev);
	cfg80211_connect_result(ndev, NULL, NULL, 0, NULL, 0,
				WLAN_STATUS_UNSPECIFIED_FAILURE, GFP_KERNEL);
	clear_bit(wil_vif_fwconnecting, vif->status);
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
	struct net_device *ndev = wil->main_ndev;
	struct wireless_dev *wdev;

	wil_dbg_misc(wil, "fw error worker\n");

	if (!ndev || !(ndev->flags & IFF_UP)) {
		wil_info(wil, "No recovery - interface is down\n");
		return;
	}
	wdev = ndev->ieee80211_ptr;

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
	/* Needs adaptation for multiple VIFs
	 * need to go over all VIFs and consider the appropriate
	 * recovery.
	 */
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

static int wil_find_free_ring(struct wil6210_priv *wil)
{
	int i;
	int min_ring_id = wil_get_min_tx_ring_id(wil);

	for (i = min_ring_id; i < WIL6210_MAX_TX_RINGS; i++) {
		if (!wil->ring_tx[i].va)
			return i;
	}
	return -EINVAL;
}

int wil_ring_init_tx(struct wil6210_vif *vif, int cid)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc = -EINVAL, ringid;

	if (cid < 0) {
		wil_err(wil, "No connection pending\n");
		goto out;
	}
	ringid = wil_find_free_ring(wil);
	if (ringid < 0) {
		wil_err(wil, "No free vring found\n");
		goto out;
	}

	wil_dbg_wmi(wil, "Configure for connection CID %d MID %d ring %d\n",
		    cid, vif->mid, ringid);

	rc = wil->txrx_ops.ring_init_tx(vif, ringid, 1 << tx_ring_order,
					cid, 0);
	if (rc)
		wil_err(wil, "init TX for CID %d MID %d vring %d failed\n",
			cid, vif->mid, ringid);

out:
	return rc;
}

int wil_bcast_init(struct wil6210_vif *vif)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int ri = vif->bcast_ring, rc;

	if (ri >= 0 && wil->ring_tx[ri].va)
		return 0;

	ri = wil_find_free_ring(wil);
	if (ri < 0)
		return ri;

	vif->bcast_ring = ri;
	rc = wil->txrx_ops.ring_init_bcast(vif, ri, 1 << bcast_ring_order);
	if (rc)
		vif->bcast_ring = -1;

	return rc;
}

void wil_bcast_fini(struct wil6210_vif *vif)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int ri = vif->bcast_ring;

	if (ri < 0)
		return;

	vif->bcast_ring = -1;
	wil_ring_fini_tx(wil, ri);
}

void wil_bcast_fini_all(struct wil6210_priv *wil)
{
	int i;
	struct wil6210_vif *vif;

	for (i = 0; i < wil->max_vifs; i++) {
		vif = wil->vifs[i];
		if (vif)
			wil_bcast_fini(vif);
	}
}

int wil_priv_init(struct wil6210_priv *wil)
{
	uint i;

	wil_dbg_misc(wil, "priv_init\n");

	memset(wil->sta, 0, sizeof(wil->sta));
	for (i = 0; i < WIL6210_MAX_CID; i++) {
		spin_lock_init(&wil->sta[i].tid_rx_lock);
		wil->sta[i].mid = U8_MAX;
	}

	for (i = 0; i < WIL6210_MAX_TX_RINGS; i++) {
		spin_lock_init(&wil->ring_tx_data[i].lock);
		wil->ring2cid_tid[i][0] = WIL6210_MAX_CID;
	}

	mutex_init(&wil->mutex);
	mutex_init(&wil->vif_mutex);
	mutex_init(&wil->wmi_mutex);
	mutex_init(&wil->halp.lock);

	init_completion(&wil->wmi_ready);
	init_completion(&wil->wmi_call);
	init_completion(&wil->halp.comp);

	INIT_WORK(&wil->wmi_event_worker, wmi_event_worker);
	INIT_WORK(&wil->fw_error_worker, wil_fw_error_worker);

	INIT_LIST_HEAD(&wil->pending_wmi_ev);
	spin_lock_init(&wil->wmi_ev_lock);
	spin_lock_init(&wil->net_queue_lock);
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
	wil->ring_idle_trsh = 16;

	wil->reply_mid = U8_MAX;
	wil->max_vifs = 1;

	/* edma configuration can be updated via debugfs before allocation */
	wil->num_rx_status_rings = WIL_DEFAULT_NUM_RX_STATUS_RINGS;
	wil->tx_status_ring_order = WIL_TX_SRING_SIZE_ORDER_DEFAULT;

	/* Rx status ring size should be bigger than the number of RX buffers
	 * in order to prevent backpressure on the status ring, which may
	 * cause HW freeze.
	 */
	wil->rx_status_ring_order = WIL_RX_SRING_SIZE_ORDER_DEFAULT;
	/* Number of RX buffer IDs should be bigger than the RX descriptor
	 * ring size as in HW reorder flow, the HW can consume additional
	 * buffers before releasing the previous ones.
	 */
	wil->rx_buff_id_count = WIL_RX_BUFF_ARR_SIZE_DEFAULT;

	wil->amsdu_en = 1;

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
 * @vif: virtual interface context
 * @bssid: peer to disconnect, NULL to disconnect all
 * @reason_code: Reason code for the Disassociation frame
 * @from_event: whether is invoked from FW event handler
 *
 * Disconnect and release associated resources. If invoked not from the
 * FW event handler, issue WMI command(s) to trigger MAC disconnect.
 */
void wil6210_disconnect(struct wil6210_vif *vif, const u8 *bssid,
			u16 reason_code, bool from_event)
{
	struct wil6210_priv *wil = vif_to_wil(vif);

	wil_dbg_misc(wil, "disconnect\n");

	del_timer_sync(&vif->connect_timer);
	_wil6210_disconnect(vif, bssid, reason_code, from_event);
}

void wil_priv_deinit(struct wil6210_priv *wil)
{
	wil_dbg_misc(wil, "priv_deinit\n");

	wil_set_recovery_state(wil, fw_recovery_idle);
	cancel_work_sync(&wil->fw_error_worker);
	wmi_event_flush(wil);
	destroy_workqueue(wil->wq_service);
	destroy_workqueue(wil->wmi_wq);
}

static void wil_shutdown_bl(struct wil6210_priv *wil)
{
	u32 val;

	wil_s(wil, RGF_USER_BL +
	      offsetof(struct bl_dedicated_registers_v1,
		       bl_shutdown_handshake), BL_SHUTDOWN_HS_GRTD);

	usleep_range(100, 150);

	val = wil_r(wil, RGF_USER_BL +
		    offsetof(struct bl_dedicated_registers_v1,
			     bl_shutdown_handshake));
	if (val & BL_SHUTDOWN_HS_RTD) {
		wil_dbg_misc(wil, "BL is ready for halt\n");
		return;
	}

	wil_err(wil, "BL did not report ready for halt\n");
}

/* this format is used by ARC embedded CPU for instruction memory */
static inline u32 ARC_me_imm32(u32 d)
{
	return ((d & 0xffff0000) >> 16) | ((d & 0x0000ffff) << 16);
}

/* defines access to interrupt vectors for wil_freeze_bl */
#define ARC_IRQ_VECTOR_OFFSET(N)	((N) * 8)
/* ARC long jump instruction */
#define ARC_JAL_INST			(0x20200f80)

static void wil_freeze_bl(struct wil6210_priv *wil)
{
	u32 jal, upc, saved;
	u32 ivt3 = ARC_IRQ_VECTOR_OFFSET(3);

	jal = wil_r(wil, wil->iccm_base + ivt3);
	if (jal != ARC_me_imm32(ARC_JAL_INST)) {
		wil_dbg_misc(wil, "invalid IVT entry found, skipping\n");
		return;
	}

	/* prevent the target from entering deep sleep
	 * and disabling memory access
	 */
	saved = wil_r(wil, RGF_USER_USAGE_8);
	wil_w(wil, RGF_USER_USAGE_8, saved | BIT_USER_PREVENT_DEEP_SLEEP);
	usleep_range(20, 25); /* let the BL process the bit */

	/* redirect to endless loop in the INT_L1 context and let it trap */
	wil_w(wil, wil->iccm_base + ivt3 + 4, ARC_me_imm32(ivt3));
	usleep_range(20, 25); /* let the BL get into the trap */

	/* verify the BL is frozen */
	upc = wil_r(wil, RGF_USER_CPU_PC);
	if (upc < ivt3 || (upc > (ivt3 + 8)))
		wil_dbg_misc(wil, "BL freeze failed, PC=0x%08X\n", upc);

	wil_w(wil, RGF_USER_USAGE_8, saved);
}

static void wil_bl_prepare_halt(struct wil6210_priv *wil)
{
	u32 tmp, ver;

	/* before halting device CPU driver must make sure BL is not accessing
	 * host memory. This is done differently depending on BL version:
	 * 1. For very old BL versions the procedure is skipped
	 * (not supported).
	 * 2. For old BL version we use a special trick to freeze the BL
	 * 3. For new BL versions we shutdown the BL using handshake procedure.
	 */
	tmp = wil_r(wil, RGF_USER_BL +
		    offsetof(struct bl_dedicated_registers_v0,
			     boot_loader_struct_version));
	if (!tmp) {
		wil_dbg_misc(wil, "old BL, skipping halt preparation\n");
		return;
	}

	tmp = wil_r(wil, RGF_USER_BL +
		    offsetof(struct bl_dedicated_registers_v1,
			     bl_shutdown_handshake));
	ver = BL_SHUTDOWN_HS_PROT_VER(tmp);

	if (ver > 0)
		wil_shutdown_bl(wil);
	else
		wil_freeze_bl(wil);
}

static inline void wil_halt_cpu(struct wil6210_priv *wil)
{
	if (wil->hw_version >= HW_VER_TALYN_MB) {
		wil_w(wil, RGF_USER_USER_CPU_0_TALYN_MB,
		      BIT_USER_USER_CPU_MAN_RST);
		wil_w(wil, RGF_USER_MAC_CPU_0_TALYN_MB,
		      BIT_USER_MAC_CPU_MAN_RST);
	} else {
		wil_w(wil, RGF_USER_USER_CPU_0, BIT_USER_USER_CPU_MAN_RST);
		wil_w(wil, RGF_USER_MAC_CPU_0,  BIT_USER_MAC_CPU_MAN_RST);
	}
}

static inline void wil_release_cpu(struct wil6210_priv *wil)
{
	/* Start CPU */
	if (wil->hw_version >= HW_VER_TALYN_MB)
		wil_w(wil, RGF_USER_USER_CPU_0_TALYN_MB, 1);
	else
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

static int wil_wait_device_ready(struct wil6210_priv *wil, int no_flash)
{
	int delay = 0;
	u32 x, x1 = 0;

	/* wait until device ready. */
	if (no_flash) {
		msleep(PMU_READY_DELAY_MS);

		wil_dbg_misc(wil, "Reset completed\n");
	} else {
		do {
			msleep(RST_DELAY);
			x = wil_r(wil, RGF_USER_BL +
				  offsetof(struct bl_dedicated_registers_v0,
					   boot_loader_ready));
			if (x1 != x) {
				wil_dbg_misc(wil, "BL.ready 0x%08x => 0x%08x\n",
					     x1, x);
				x1 = x;
			}
			if (delay++ > RST_COUNT) {
				wil_err(wil, "Reset not completed, bl.ready 0x%08x\n",
					x);
				return -ETIME;
			}
		} while (x != BL_READY);

		wil_dbg_misc(wil, "Reset completed in %d ms\n",
			     delay * RST_DELAY);
	}

	return 0;
}

static int wil_wait_device_ready_talyn_mb(struct wil6210_priv *wil)
{
	u32 otp_hw;
	u8 signature_status;
	bool otp_signature_err;
	bool hw_section_done;
	u32 otp_qc_secured;
	int delay = 0;

	/* Wait for OTP signature test to complete */
	usleep_range(2000, 2200);

	wil->boot_config = WIL_BOOT_ERR;

	/* Poll until OTP signature status is valid.
	 * In vanilla and development modes, when signature test is complete
	 * HW sets BIT_OTP_SIGNATURE_ERR_TALYN_MB.
	 * In production mode BIT_OTP_SIGNATURE_ERR_TALYN_MB remains 0, poll
	 * for signature status change to 2 or 3.
	 */
	do {
		otp_hw = wil_r(wil, RGF_USER_OTP_HW_RD_MACHINE_1);
		signature_status = WIL_GET_BITS(otp_hw, 8, 9);
		otp_signature_err = otp_hw & BIT_OTP_SIGNATURE_ERR_TALYN_MB;

		if (otp_signature_err &&
		    signature_status == WIL_SIG_STATUS_VANILLA) {
			wil->boot_config = WIL_BOOT_VANILLA;
			break;
		}
		if (otp_signature_err &&
		    signature_status == WIL_SIG_STATUS_DEVELOPMENT) {
			wil->boot_config = WIL_BOOT_DEVELOPMENT;
			break;
		}
		if (!otp_signature_err &&
		    signature_status == WIL_SIG_STATUS_PRODUCTION) {
			wil->boot_config = WIL_BOOT_PRODUCTION;
			break;
		}
		if  (!otp_signature_err &&
		     signature_status ==
		     WIL_SIG_STATUS_CORRUPTED_PRODUCTION) {
			/* Unrecognized OTP signature found. Possibly a
			 * corrupted production signature, access control
			 * is applied as in production mode, therefore
			 * do not fail
			 */
			wil->boot_config = WIL_BOOT_PRODUCTION;
			break;
		}
		if (delay++ > OTP_HW_COUNT)
			break;

		usleep_range(OTP_HW_DELAY, OTP_HW_DELAY + 10);
	} while (!otp_signature_err && signature_status == 0);

	if (wil->boot_config == WIL_BOOT_ERR) {
		wil_err(wil,
			"invalid boot config, signature_status %d otp_signature_err %d\n",
			signature_status, otp_signature_err);
		return -ETIME;
	}

	wil_dbg_misc(wil,
		     "signature test done in %d usec, otp_hw 0x%x, boot_config %d\n",
		     delay * OTP_HW_DELAY, otp_hw, wil->boot_config);

	if (wil->boot_config == WIL_BOOT_VANILLA)
		/* Assuming not SPI boot (currently not supported) */
		goto out;

	hw_section_done = otp_hw & BIT_OTP_HW_SECTION_DONE_TALYN_MB;
	delay = 0;

	while (!hw_section_done) {
		msleep(RST_DELAY);

		otp_hw = wil_r(wil, RGF_USER_OTP_HW_RD_MACHINE_1);
		hw_section_done = otp_hw & BIT_OTP_HW_SECTION_DONE_TALYN_MB;

		if (delay++ > RST_COUNT) {
			wil_err(wil, "TO waiting for hw_section_done\n");
			return -ETIME;
		}
	}

	wil_dbg_misc(wil, "HW section done in %d ms\n", delay * RST_DELAY);

	otp_qc_secured = wil_r(wil, RGF_OTP_QC_SECURED);
	wil->secured_boot = otp_qc_secured & BIT_BOOT_FROM_ROM ? 1 : 0;
	wil_dbg_misc(wil, "secured boot is %sabled\n",
		     wil->secured_boot ? "en" : "dis");

out:
	wil_dbg_misc(wil, "Reset completed\n");

	return 0;
}

static int wil_target_reset(struct wil6210_priv *wil, int no_flash)
{
	u32 x;
	int rc;

	wil_dbg_misc(wil, "Resetting \"%s\"...\n", wil->hw_name);

	if (wil->hw_version < HW_VER_TALYN) {
		/* Clear MAC link up */
		wil_s(wil, RGF_HP_CTRL, BIT(15));
		wil_s(wil, RGF_USER_CLKS_CTL_SW_RST_MASK_0,
		      BIT_HPAL_PERST_FROM_PAD);
		wil_s(wil, RGF_USER_CLKS_CTL_SW_RST_MASK_0, BIT_CAR_PERST_RST);
	}

	wil_halt_cpu(wil);

	if (!no_flash) {
		/* clear all boot loader "ready" bits */
		wil_w(wil, RGF_USER_BL +
		      offsetof(struct bl_dedicated_registers_v0,
			       boot_loader_ready), 0);
		/* this should be safe to write even with old BLs */
		wil_w(wil, RGF_USER_BL +
		      offsetof(struct bl_dedicated_registers_v1,
			       bl_shutdown_handshake), 0);
	}
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

	if (wil->hw_version >= HW_VER_TALYN_MB) {
		wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0x7e000000);
		wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_1, 0x0000003f);
		wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_3, 0xc00000f0);
		wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_0, 0xffe7fe00);
	} else {
		wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0xfe000000);
		wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_1, 0x0000003f);
		wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_3, 0x000000f0);
		wil_w(wil, RGF_USER_CLKS_CTL_SW_RST_VEC_0, 0xffe7fe00);
	}

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

	if (wil->hw_version == HW_VER_TALYN_MB)
		rc = wil_wait_device_ready_talyn_mb(wil);
	else
		rc = wil_wait_device_ready(wil, no_flash);
	if (rc)
		return rc;

	wil_c(wil, RGF_USER_CLKS_CTL_0, BIT_USER_CLKS_RST_PWGD);

	/* enable fix for HW bug related to the SA/DA swap in AP Rx */
	wil_s(wil, RGF_DMA_OFUL_NID_0, BIT_DMA_OFUL_NID_0_RX_EXT_TR_EN |
	      BIT_DMA_OFUL_NID_0_RX_EXT_A3_SRC);

	if (wil->hw_version < HW_VER_TALYN_MB && no_flash) {
		/* Reset OTP HW vectors to fit 40MHz */
		wil_w(wil, RGF_USER_XPM_IFC_RD_TIME1, 0x60001);
		wil_w(wil, RGF_USER_XPM_IFC_RD_TIME2, 0x20027);
		wil_w(wil, RGF_USER_XPM_IFC_RD_TIME3, 0x1);
		wil_w(wil, RGF_USER_XPM_IFC_RD_TIME4, 0x20027);
		wil_w(wil, RGF_USER_XPM_IFC_RD_TIME5, 0x30003);
		wil_w(wil, RGF_USER_XPM_IFC_RD_TIME6, 0x20002);
		wil_w(wil, RGF_USER_XPM_IFC_RD_TIME7, 0x60001);
		wil_w(wil, RGF_USER_XPM_IFC_RD_TIME8, 0x60001);
		wil_w(wil, RGF_USER_XPM_IFC_RD_TIME9, 0x60001);
		wil_w(wil, RGF_USER_XPM_IFC_RD_TIME10, 0x60001);
		wil_w(wil, RGF_USER_XPM_RD_DOUT_SAMPLE_TIME, 0x57);
	}

	return 0;
}

static void wil_collect_fw_info(struct wil6210_priv *wil)
{
	struct wiphy *wiphy = wil_to_wiphy(wil);
	u8 retry_short;
	int rc;

	wil_refresh_fw_capabilities(wil);

	rc = wmi_get_mgmt_retry(wil, &retry_short);
	if (!rc) {
		wiphy->retry_short = retry_short;
		wil_dbg_misc(wil, "FW retry_short: %d\n", retry_short);
	}
}

void wil_refresh_fw_capabilities(struct wil6210_priv *wil)
{
	struct wiphy *wiphy = wil_to_wiphy(wil);
	int features;

	wil->keep_radio_on_during_sleep =
		test_bit(WIL_PLATFORM_CAPA_RADIO_ON_IN_SUSPEND,
			 wil->platform_capa) &&
		test_bit(WMI_FW_CAPABILITY_D3_SUSPEND, wil->fw_capabilities);

	wil_info(wil, "keep_radio_on_during_sleep (%d)\n",
		 wil->keep_radio_on_during_sleep);

	if (test_bit(WMI_FW_CAPABILITY_RSSI_REPORTING, wil->fw_capabilities))
		wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	else
		wiphy->signal_type = CFG80211_SIGNAL_TYPE_UNSPEC;

	if (test_bit(WMI_FW_CAPABILITY_PNO, wil->fw_capabilities)) {
		wiphy->max_sched_scan_reqs = 1;
		wiphy->max_sched_scan_ssids = WMI_MAX_PNO_SSID_NUM;
		wiphy->max_match_sets = WMI_MAX_PNO_SSID_NUM;
		wiphy->max_sched_scan_ie_len = WMI_MAX_IE_LEN;
		wiphy->max_sched_scan_plans = WMI_MAX_PLANS_NUM;
	}

	if (test_bit(WMI_FW_CAPABILITY_TX_REQ_EXT, wil->fw_capabilities))
		wiphy->flags |= WIPHY_FLAG_OFFCHAN_TX;

	if (wil->platform_ops.set_features) {
		features = (test_bit(WMI_FW_CAPABILITY_REF_CLOCK_CONTROL,
				     wil->fw_capabilities) &&
			    test_bit(WIL_PLATFORM_CAPA_EXT_CLK,
				     wil->platform_capa)) ?
			BIT(WIL_PLATFORM_FEATURE_FW_EXT_CLK_CONTROL) : 0;

		if (wil->n_msi == 3)
			features |= BIT(WIL_PLATFORM_FEATURE_TRIPLE_MSI);

		wil->platform_ops.set_features(wil->platform_handle, features);
	}

	if (test_bit(WMI_FW_CAPABILITY_BACK_WIN_SIZE_64,
		     wil->fw_capabilities)) {
		wil->max_agg_wsize = WIL_MAX_AGG_WSIZE_64;
		wil->max_ampdu_size = WIL_MAX_AMPDU_SIZE_128;
	} else {
		wil->max_agg_wsize = WIL_MAX_AGG_WSIZE;
		wil->max_ampdu_size = WIL_MAX_AMPDU_SIZE;
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

/* construct actual board file name to use */
void wil_get_board_file(struct wil6210_priv *wil, char *buf, size_t len)
{
	const char *board_file;
	const char *wil_talyn_fw_name = ftm_mode ? WIL_FW_NAME_FTM_TALYN :
			      WIL_FW_NAME_TALYN;

	if (wil->board_file) {
		board_file = wil->board_file;
	} else {
		/* If specific FW file is used for Talyn,
		 * use specific board file
		 */
		if (strcmp(wil->wil_fw_name, wil_talyn_fw_name) == 0)
			board_file = WIL_BRD_NAME_TALYN;
		else
			board_file = WIL_BOARD_FILE_NAME;
	}

	strlcpy(buf, board_file, len);
}

static int wil_get_bl_info(struct wil6210_priv *wil)
{
	struct net_device *ndev = wil->main_ndev;
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

static int wil_get_otp_info(struct wil6210_priv *wil)
{
	struct net_device *ndev = wil->main_ndev;
	struct wiphy *wiphy = wil_to_wiphy(wil);
	u8 mac[8];
	int mac_addr;

	if (wil->hw_version >= HW_VER_TALYN_MB)
		mac_addr = RGF_OTP_MAC_TALYN_MB;
	else
		mac_addr = RGF_OTP_MAC;

	wil_memcpy_fromio_32(mac, wil->csr + HOSTADDR(mac_addr),
			     sizeof(mac));
	if (!is_valid_ether_addr(mac)) {
		wil_err(wil, "Invalid MAC %pM\n", mac);
		return -EINVAL;
	}

	ether_addr_copy(ndev->perm_addr, mac);
	ether_addr_copy(wiphy->perm_addr, mac);
	if (!is_valid_ether_addr(ndev->dev_addr))
		ether_addr_copy(ndev->dev_addr, mac);

	return 0;
}

static int wil_wait_for_fw_ready(struct wil6210_priv *wil)
{
	ulong to = msecs_to_jiffies(2000);
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

void wil_abort_scan(struct wil6210_vif *vif, bool sync)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc;
	struct cfg80211_scan_info info = {
		.aborted = true,
	};

	lockdep_assert_held(&wil->vif_mutex);

	if (!vif->scan_request)
		return;

	wil_dbg_misc(wil, "Abort scan_request 0x%p\n", vif->scan_request);
	del_timer_sync(&vif->scan_timer);
	mutex_unlock(&wil->vif_mutex);
	rc = wmi_abort_scan(vif);
	if (!rc && sync)
		wait_event_interruptible_timeout(wil->wq, !vif->scan_request,
						 msecs_to_jiffies(
						 WAIT_FOR_SCAN_ABORT_MS));

	mutex_lock(&wil->vif_mutex);
	if (vif->scan_request) {
		cfg80211_scan_done(vif->scan_request, &info);
		vif->scan_request = NULL;
	}
}

void wil_abort_scan_all_vifs(struct wil6210_priv *wil, bool sync)
{
	int i;

	lockdep_assert_held(&wil->vif_mutex);

	for (i = 0; i < wil->max_vifs; i++) {
		struct wil6210_vif *vif = wil->vifs[i];

		if (vif)
			wil_abort_scan(vif, sync);
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
	if (wil->hw_version < HW_VER_TALYN_MB) {
		wil_s(wil, RGF_CAF_ICR + offsetof(struct RGF_ICR, ICR), 0);
		wil_w(wil, RGF_CAF_ICR + offsetof(struct RGF_ICR, IMV), ~0);
	} else {
		wil_s(wil,
		      RGF_CAF_ICR_TALYN_MB + offsetof(struct RGF_ICR, ICR), 0);
		wil_w(wil, RGF_CAF_ICR_TALYN_MB +
		      offsetof(struct RGF_ICR, IMV), ~0);
	}
	/* clear PAL_UNIT_ICR (potential D0->D3 leftover)
	 * In Talyn-MB host cannot access this register due to
	 * access control, hence PAL_UNIT_ICR is cleared by the FW
	 */
	if (wil->hw_version < HW_VER_TALYN_MB)
		wil_s(wil, RGF_PAL_UNIT_ICR + offsetof(struct RGF_ICR, ICR),
		      0);

	if (wil->fw_calib_result > 0) {
		__le32 val = cpu_to_le32(wil->fw_calib_result |
						(CALIB_RESULT_SIGNATURE << 8));
		wil_w(wil, RGF_USER_FW_CALIB_RESULT, (u32 __force)val);
	}
}

static int wil_restore_vifs(struct wil6210_priv *wil)
{
	struct wil6210_vif *vif;
	struct net_device *ndev;
	struct wireless_dev *wdev;
	int i, rc;

	for (i = 0; i < wil->max_vifs; i++) {
		vif = wil->vifs[i];
		if (!vif)
			continue;
		vif->ap_isolate = 0;
		if (vif->mid) {
			ndev = vif_to_ndev(vif);
			wdev = vif_to_wdev(vif);
			rc = wmi_port_allocate(wil, vif->mid, ndev->dev_addr,
					       wdev->iftype);
			if (rc) {
				wil_err(wil, "fail to restore VIF %d type %d, rc %d\n",
					i, wdev->iftype, rc);
				return rc;
			}
		}
	}

	return 0;
}

/*
 * We reset all the structures, and we reset the UMAC.
 * After calling this routine, you're expected to reload
 * the firmware.
 */
int wil_reset(struct wil6210_priv *wil, bool load_fw)
{
	int rc, i;
	unsigned long status_flags = BIT(wil_status_resetting);
	int no_flash;
	struct wil6210_vif *vif;

	wil_dbg_misc(wil, "reset\n");

	WARN_ON(!mutex_is_locked(&wil->mutex));
	WARN_ON(test_bit(wil_status_napi_en, wil->status));

	if (debug_fw) {
		static const u8 mac[ETH_ALEN] = {
			0x00, 0xde, 0xad, 0x12, 0x34, 0x56,
		};
		struct net_device *ndev = wil->main_ndev;

		ether_addr_copy(ndev->perm_addr, mac);
		ether_addr_copy(ndev->dev_addr, ndev->perm_addr);
		return 0;
	}

	if (wil->hw_version == HW_VER_UNKNOWN)
		return -ENODEV;

	if (test_bit(WIL_PLATFORM_CAPA_T_PWR_ON_0, wil->platform_capa)) {
		wil_dbg_misc(wil, "Notify FW to set T_POWER_ON=0\n");
		wil_s(wil, RGF_USER_USAGE_8, BIT_USER_SUPPORT_T_POWER_ON_0);
	}

	if (test_bit(WIL_PLATFORM_CAPA_EXT_CLK, wil->platform_capa)) {
		wil_dbg_misc(wil, "Notify FW on ext clock configuration\n");
		wil_s(wil, RGF_USER_USAGE_8, BIT_USER_EXT_CLK);
	}

	if (wil->platform_ops.notify) {
		rc = wil->platform_ops.notify(wil->platform_handle,
					      WIL_PLATFORM_EVT_PRE_RESET);
		if (rc)
			wil_err(wil, "PRE_RESET platform notify failed, rc %d\n",
				rc);
	}

	set_bit(wil_status_resetting, wil->status);
	if (test_bit(wil_status_collecting_dumps, wil->status)) {
		/* Device collects crash dump, cancel the reset.
		 * following crash dump collection, reset would take place.
		 */
		wil_dbg_misc(wil, "reject reset while collecting crash dump\n");
		rc = -EBUSY;
		goto out;
	}

	mutex_lock(&wil->vif_mutex);
	wil_abort_scan_all_vifs(wil, false);
	mutex_unlock(&wil->vif_mutex);

	for (i = 0; i < wil->max_vifs; i++) {
		vif = wil->vifs[i];
		if (vif) {
			cancel_work_sync(&vif->disconnect_worker);
			wil6210_disconnect(vif, NULL,
					   WLAN_REASON_DEAUTH_LEAVING, false);
		}
	}
	wil_bcast_fini_all(wil);

	/* Disable device led before reset*/
	wmi_led_cfg(wil, false);

	/* prevent NAPI from being scheduled and prevent wmi commands */
	mutex_lock(&wil->wmi_mutex);
	if (test_bit(wil_status_suspending, wil->status))
		status_flags |= BIT(wil_status_suspending);
	bitmap_and(wil->status, wil->status, &status_flags,
		   wil_status_last);
	wil_dbg_misc(wil, "wil->status (0x%lx)\n", *wil->status);
	mutex_unlock(&wil->wmi_mutex);

	wil_mask_irq(wil);

	wmi_event_flush(wil);

	flush_workqueue(wil->wq_service);
	flush_workqueue(wil->wmi_wq);

	no_flash = test_bit(hw_capa_no_flash, wil->hw_capa);
	if (!no_flash)
		wil_bl_crash_info(wil, false);
	wil_disable_irq(wil);
	rc = wil_target_reset(wil, no_flash);
	wil6210_clear_irq(wil);
	wil_enable_irq(wil);
	wil->txrx_ops.rx_fini(wil);
	wil->txrx_ops.tx_fini(wil);
	if (rc) {
		if (!no_flash)
			wil_bl_crash_info(wil, true);
		goto out;
	}

	if (no_flash) {
		rc = wil_get_otp_info(wil);
	} else {
		rc = wil_get_bl_info(wil);
		if (rc == -EAGAIN && !load_fw)
			/* ignore RF error if not going up */
			rc = 0;
	}
	if (rc)
		goto out;

	wil_set_oob_mode(wil, oob_mode);
	if (load_fw) {
		char board_file[WIL_BOARD_FILE_MAX_NAMELEN];

		if  (wil->secured_boot) {
			wil_err(wil, "secured boot is not supported\n");
			return -ENOTSUPP;
		}

		board_file[0] = '\0';
		wil_get_board_file(wil, board_file, sizeof(board_file));
		wil_info(wil, "Use firmware <%s> + board <%s>\n",
			 wil->wil_fw_name, board_file);

		if (!no_flash)
			wil_bl_prepare_halt(wil);

		wil_halt_cpu(wil);
		memset(wil->fw_version, 0, sizeof(wil->fw_version));
		/* Loading f/w from the file */
		rc = wil_request_firmware(wil, wil->wil_fw_name, true);
		if (rc)
			goto out;
		if (wil->brd_file_addr)
			rc = wil_request_board(wil, board_file);
		else
			rc = wil_request_firmware(wil, board_file, true);
		if (rc)
			goto out;

		wil_pre_fw_config(wil);
		wil_release_cpu(wil);
	}

	/* init after reset */
	reinit_completion(&wil->wmi_ready);
	reinit_completion(&wil->wmi_call);
	reinit_completion(&wil->halp.comp);

	clear_bit(wil_status_resetting, wil->status);

	if (load_fw) {
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

		wil->txrx_ops.configure_interrupt_moderation(wil);

		/* Enable OFU rdy valid bug fix, to prevent hang in oful34_rx
		 * while there is back-pressure from Host during RX
		 */
		if (wil->hw_version >= HW_VER_TALYN_MB)
			wil_s(wil, RGF_DMA_MISC_CTL,
			      BIT_OFUL34_RDY_VALID_BUG_FIX_EN);

		rc = wil_restore_vifs(wil);
		if (rc) {
			wil_err(wil, "failed to restore vifs, rc %d\n", rc);
			return rc;
		}

		wil_collect_fw_info(wil);

		if (wil->ps_profile != WMI_PS_PROFILE_TYPE_DEFAULT)
			wil_ps_update(wil, wil->ps_profile);

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

out:
	clear_bit(wil_status_resetting, wil->status);
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
	struct net_device *ndev = wil->main_ndev;
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	int rc;

	WARN_ON(!mutex_is_locked(&wil->mutex));

	rc = wil_reset(wil, true);
	if (rc)
		return rc;

	/* Rx RING. After MAC and beacon */
	rc = wil->txrx_ops.rx_init(wil, 1 << rx_ring_order);
	if (rc)
		return rc;

	rc = wil->txrx_ops.tx_init(wil);
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

	mutex_lock(&wil->vif_mutex);
	wil_p2p_stop_radio_operations(wil);
	wil_abort_scan_all_vifs(wil, false);
	mutex_unlock(&wil->vif_mutex);

	return wil_reset(wil, false);
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

int wil_find_cid(struct wil6210_priv *wil, u8 mid, const u8 *mac)
{
	int i;
	int rc = -ENOENT;

	for (i = 0; i < ARRAY_SIZE(wil->sta); i++) {
		if (wil->sta[i].mid == mid &&
		    wil->sta[i].status != wil_sta_unused &&
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

void wil_init_txrx_ops(struct wil6210_priv *wil)
{
	if (wil->use_enhanced_dma_hw)
		wil_init_txrx_ops_edma(wil);
	else
		wil_init_txrx_ops_legacy_dma(wil);
}
