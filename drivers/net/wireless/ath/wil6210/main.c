/*
 * Copyright (c) 2012-2014 Qualcomm Atheros, Inc.
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

#define WAIT_FOR_DISCONNECT_TIMEOUT_MS 2000
#define WAIT_FOR_DISCONNECT_INTERVAL_MS 10

bool no_fw_recovery;
module_param(no_fw_recovery, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(no_fw_recovery, " disable automatic FW error recovery");

static bool no_fw_load = true;
module_param(no_fw_load, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(no_fw_load, " do not download FW, use one in on-card flash.");

static unsigned int itr_trsh = WIL6210_ITR_TRSH_DEFAULT;

module_param(itr_trsh, uint, S_IRUGO);
MODULE_PARM_DESC(itr_trsh, " Interrupt moderation threshold, usecs.");

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

	/* size_t is unsigned, if (count%4 != 0) it will wrap */
	for (count += 4; count > 4; count -= 4)
		*d++ = __raw_readl(s++);
}

void wil_memcpy_toio_32(volatile void __iomem *dst, const void *src,
			size_t count)
{
	volatile u32 __iomem *d = dst;
	const u32 *s = src;

	for (count += 4; count > 4; count -= 4)
		__raw_writel(*s++, d++);
}

static void wil_disconnect_cid(struct wil6210_priv *wil, int cid)
{
	uint i;
	struct net_device *ndev = wil_to_ndev(wil);
	struct wireless_dev *wdev = wil->wdev;
	struct wil_sta_info *sta = &wil->sta[cid];

	wil_dbg_misc(wil, "%s(CID %d, status %d)\n", __func__, cid,
		     sta->status);

	sta->data_port_open = false;
	if (sta->status != wil_sta_unused) {
		wmi_disconnect_sta(wil, sta->addr, WLAN_REASON_DEAUTH_LEAVING);
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

	for (i = 0; i < WIL_STA_TID_NUM; i++) {
		struct wil_tid_ampdu_rx *r;
		unsigned long flags;

		spin_lock_irqsave(&sta->tid_rx_lock, flags);

		r = sta->tid_rx[i];
		sta->tid_rx[i] = NULL;
		wil_tid_ampdu_rx_free(wil, r);

		spin_unlock_irqrestore(&sta->tid_rx_lock, flags);
	}
	for (i = 0; i < ARRAY_SIZE(wil->vring_tx); i++) {
		if (wil->vring2cid_tid[i][0] == cid)
			wil_vring_fini_tx(wil, i);
	}
	memset(&sta->stats, 0, sizeof(sta->stats));
}

static void _wil6210_disconnect(struct wil6210_priv *wil, const u8 *bssid)
{
	int cid = -ENOENT;
	struct net_device *ndev = wil_to_ndev(wil);
	struct wireless_dev *wdev = wil->wdev;

	might_sleep();
	if (bssid) {
		cid = wil_find_cid(wil, bssid);
		wil_dbg_misc(wil, "%s(%pM, CID %d)\n", __func__, bssid, cid);
	} else {
		wil_dbg_misc(wil, "%s(all)\n", __func__);
	}

	if (cid >= 0) /* disconnect 1 peer */
		wil_disconnect_cid(wil, cid);
	else /* disconnect all */
		for (cid = 0; cid < WIL6210_MAX_CID; cid++)
			wil_disconnect_cid(wil, cid);

	/* link state */
	switch (wdev->iftype) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		wil_link_off(wil);
		if (test_bit(wil_status_fwconnected, &wil->status)) {
			clear_bit(wil_status_fwconnected, &wil->status);
			cfg80211_disconnected(ndev,
					      WLAN_STATUS_UNSPECIFIED_FAILURE,
					      NULL, 0, GFP_KERNEL);
		} else if (test_bit(wil_status_fwconnecting, &wil->status)) {
			cfg80211_connect_result(ndev, bssid, NULL, 0, NULL, 0,
						WLAN_STATUS_UNSPECIFIED_FAILURE,
						GFP_KERNEL);
		}
		clear_bit(wil_status_fwconnecting, &wil->status);
		break;
	default:
		break;
	}
}

static void wil_disconnect_worker(struct work_struct *work)
{
	struct wil6210_priv *wil = container_of(work,
			struct wil6210_priv, disconnect_worker);

	mutex_lock(&wil->mutex);
	_wil6210_disconnect(wil, NULL);
	mutex_unlock(&wil->mutex);
}

static void wil_connect_timer_fn(ulong x)
{
	struct wil6210_priv *wil = (void *)x;

	wil_dbg_misc(wil, "Connect timeout\n");

	/* reschedule to thread context - disconnect won't
	 * run from atomic context
	 */
	schedule_work(&wil->disconnect_worker);
}

static void wil_scan_timer_fn(ulong x)
{
	struct wil6210_priv *wil = (void *)x;

	clear_bit(wil_status_fwready, &wil->status);
	wil_err(wil, "Scan timeout detected, start fw error recovery\n");
	schedule_work(&wil->fw_error_worker);
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
	wil_dbg_misc(wil, "%s(%d -> %d)\n", __func__,
		     wil->recovery_state, state);

	wil->recovery_state = state;
	wake_up_interruptible(&wil->wq);
}

static void wil_fw_error_worker(struct work_struct *work)
{
	struct wil6210_priv *wil = container_of(work, struct wil6210_priv,
						fw_error_worker);
	struct wireless_dev *wdev = wil->wdev;

	wil_dbg_misc(wil, "fw error worker\n");

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

	mutex_lock(&wil->mutex);
	switch (wdev->iftype) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_MONITOR:
		wil_info(wil, "fw error recovery requested (try %d)...\n",
			 wil->recovery_count);
		if (!no_fw_recovery)
			wil->recovery_state = fw_recovery_running;
		if (0 != wil_wait_for_recovery(wil))
			break;

		__wil_down(wil);
		__wil_up(wil);
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		/* recovery in these modes is done by upper layers */
		break;
	default:
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

static void wil_connect_worker(struct work_struct *work)
{
	int rc;
	struct wil6210_priv *wil = container_of(work, struct wil6210_priv,
						connect_worker);
	int cid = wil->pending_connect_cid;
	int ringid = wil_find_free_vring(wil);

	if (cid < 0) {
		wil_err(wil, "No connection pending\n");
		return;
	}

	wil_dbg_wmi(wil, "Configure for connection CID %d\n", cid);

	rc = wil_vring_init_tx(wil, ringid, WIL6210_TX_RING_SIZE, cid, 0);
	wil->pending_connect_cid = -1;
	if (rc == 0) {
		wil->sta[cid].status = wil_sta_connected;
		wil_link_on(wil);
	} else {
		wil->sta[cid].status = wil_sta_unused;
	}
}

int wil_priv_init(struct wil6210_priv *wil)
{
	uint i;

	wil_dbg_misc(wil, "%s()\n", __func__);

	memset(wil->sta, 0, sizeof(wil->sta));
	for (i = 0; i < WIL6210_MAX_CID; i++)
		spin_lock_init(&wil->sta[i].tid_rx_lock);

	mutex_init(&wil->mutex);
	mutex_init(&wil->wmi_mutex);

	init_completion(&wil->wmi_ready);
	init_completion(&wil->wmi_call);

	wil->pending_connect_cid = -1;
	setup_timer(&wil->connect_timer, wil_connect_timer_fn, (ulong)wil);
	setup_timer(&wil->scan_timer, wil_scan_timer_fn, (ulong)wil);

	INIT_WORK(&wil->connect_worker, wil_connect_worker);
	INIT_WORK(&wil->disconnect_worker, wil_disconnect_worker);
	INIT_WORK(&wil->wmi_event_worker, wmi_event_worker);
	INIT_WORK(&wil->fw_error_worker, wil_fw_error_worker);

	INIT_LIST_HEAD(&wil->pending_wmi_ev);
	spin_lock_init(&wil->wmi_ev_lock);
	init_waitqueue_head(&wil->wq);

	wil->wmi_wq = create_singlethread_workqueue(WIL_NAME"_wmi");
	if (!wil->wmi_wq)
		return -EAGAIN;

	wil->wmi_wq_conn = create_singlethread_workqueue(WIL_NAME"_connect");
	if (!wil->wmi_wq_conn) {
		destroy_workqueue(wil->wmi_wq);
		return -EAGAIN;
	}

	wil->last_fw_recovery = jiffies;
	wil->itr_trsh = itr_trsh;

	return 0;
}

void wil6210_disconnect(struct wil6210_priv *wil, const u8 *bssid)
{
	wil_dbg_misc(wil, "%s()\n", __func__);

	del_timer_sync(&wil->connect_timer);
	_wil6210_disconnect(wil, bssid);
}

void wil_priv_deinit(struct wil6210_priv *wil)
{
	wil_dbg_misc(wil, "%s()\n", __func__);

	wil_set_recovery_state(wil, fw_recovery_idle);
	del_timer_sync(&wil->scan_timer);
	cancel_work_sync(&wil->disconnect_worker);
	cancel_work_sync(&wil->fw_error_worker);
	mutex_lock(&wil->mutex);
	wil6210_disconnect(wil, NULL);
	mutex_unlock(&wil->mutex);
	wmi_event_flush(wil);
	destroy_workqueue(wil->wmi_wq_conn);
	destroy_workqueue(wil->wmi_wq);
}

/* target operations */
/* register read */
#define R(a) ioread32(wil->csr + HOSTADDR(a))
/* register write. wmb() to make sure it is completed */
#define W(a, v) do { iowrite32(v, wil->csr + HOSTADDR(a)); wmb(); } while (0)
/* register set = read, OR, write */
#define S(a, v) W(a, R(a) | v)
/* register clear = read, AND with inverted, write */
#define C(a, v) W(a, R(a) & ~v)

static inline void wil_halt_cpu(struct wil6210_priv *wil)
{
	W(RGF_USER_USER_CPU_0, BIT_USER_USER_CPU_MAN_RST);
	W(RGF_USER_MAC_CPU_0,  BIT_USER_MAC_CPU_MAN_RST);
}

static inline void wil_release_cpu(struct wil6210_priv *wil)
{
	/* Start CPU */
	W(RGF_USER_USER_CPU_0, 1);
}

static int wil_target_reset(struct wil6210_priv *wil)
{
	int delay = 0;
	u32 hw_state;
	u32 rev_id;
	bool is_sparrow = (wil->board->board == WIL_BOARD_SPARROW);

	wil_dbg_misc(wil, "Resetting \"%s\"...\n", wil->board->name);

	wil->hw_version = R(RGF_USER_FW_REV_ID);
	rev_id = wil->hw_version & 0xff;

	/* Clear MAC link up */
	S(RGF_HP_CTRL, BIT(15));
	S(RGF_USER_CLKS_CTL_SW_RST_MASK_0, BIT_HPAL_PERST_FROM_PAD);
	S(RGF_USER_CLKS_CTL_SW_RST_MASK_0, BIT_CAR_PERST_RST);

	wil_halt_cpu(wil);
	C(RGF_USER_CLKS_CTL_0, BIT_USER_CLKS_CAR_AHB_SW_SEL); /* 40 MHz */

	if (is_sparrow) {
		W(RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_0, 0x3ff81f);
		W(RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_1, 0xf);
	}

	W(RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0xFE000000);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_1, 0x0000003F);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_3, is_sparrow ? 0x000000f0 : 0x00000170);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_0, 0xFFE7FE00);

	if (is_sparrow) {
		W(RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_0, 0x0);
		W(RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_1, 0x0);
	}

	W(RGF_USER_CLKS_CTL_SW_RST_VEC_3, 0);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_1, 0);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_0, 0);

	if (is_sparrow) {
		W(RGF_USER_CLKS_CTL_SW_RST_VEC_3, 0x00000003);
		/* reset A2 PCIE AHB */
		W(RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0x00008000);
	} else {
		W(RGF_USER_CLKS_CTL_SW_RST_VEC_3, 0x00000001);
		if (rev_id == 1) {
			/* reset A1 BOTH PCIE AHB & PCIE RGF */
			W(RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0x00000080);
		} else {
			W(RGF_PCIE_LOS_COUNTER_CTL, BIT(6) | BIT(8));
			W(RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0x00008000);
		}
	}

	/* TODO: check order here!!! Erez code is different */
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_0, 0);

	/* wait until device ready. typical time is 200..250 msec */
	do {
		msleep(RST_DELAY);
		hw_state = R(RGF_USER_HW_MACHINE_STATE);
		if (delay++ > RST_COUNT) {
			wil_err(wil, "Reset not completed, hw_state 0x%08x\n",
				hw_state);
			return -ETIME;
		}
	} while (hw_state != HW_MACHINE_BOOT_DONE);

	/* TODO: Erez check rev_id != 1 */
	if (!is_sparrow && (rev_id != 1))
		W(RGF_PCIE_LOS_COUNTER_CTL, BIT(8));

	C(RGF_USER_CLKS_CTL_0, BIT_USER_CLKS_RST_PWGD);

	wil_dbg_misc(wil, "Reset completed in %d ms\n", delay * RST_DELAY);
	return 0;
}

/**
 * wil_set_itr_trsh: - apply interrupt coalescing params
 */
void wil_set_itr_trsh(struct wil6210_priv *wil)
{
	/* disable, use usec resolution */
	W(RGF_DMA_ITR_CNT_CRL, BIT_DMA_ITR_CNT_CRL_EXT_TICK);

	/* disable interrupt moderation for monitor
	 * to get better timestamp precision
	 */
	if (wil->wdev->iftype == NL80211_IFTYPE_MONITOR)
		return;

	wil_info(wil, "set ITR_TRSH = %d usec\n", wil->itr_trsh);
	W(RGF_DMA_ITR_CNT_TRSH, wil->itr_trsh);
	W(RGF_DMA_ITR_CNT_CRL, BIT_DMA_ITR_CNT_CRL_EN |
	  BIT_DMA_ITR_CNT_CRL_EXT_TICK); /* start it */
}

#undef R
#undef W
#undef S
#undef C

void wil_mbox_ring_le2cpus(struct wil6210_mbox_ring *r)
{
	le32_to_cpus(&r->base);
	le16_to_cpus(&r->entry_size);
	le16_to_cpus(&r->size);
	le32_to_cpus(&r->tail);
	le32_to_cpus(&r->head);
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

/*
 * We reset all the structures, and we reset the UMAC.
 * After calling this routine, you're expected to reload
 * the firmware.
 */
int wil_reset(struct wil6210_priv *wil)
{
	int rc;

	wil_dbg_misc(wil, "%s()\n", __func__);

	WARN_ON(!mutex_is_locked(&wil->mutex));
	WARN_ON(test_bit(wil_status_napi_en, &wil->status));

	cancel_work_sync(&wil->disconnect_worker);
	wil6210_disconnect(wil, NULL);

	wil->status = 0; /* prevent NAPI from being scheduled */

	if (wil->scan_request) {
		wil_dbg_misc(wil, "Abort scan_request 0x%p\n",
			     wil->scan_request);
		del_timer_sync(&wil->scan_timer);
		cfg80211_scan_done(wil->scan_request, true);
		wil->scan_request = NULL;
	}

	wil_mask_irq(wil);

	wmi_event_flush(wil);

	flush_workqueue(wil->wmi_wq_conn);
	flush_workqueue(wil->wmi_wq);

	rc = wil_target_reset(wil);
	wil_rx_fini(wil);
	if (rc)
		return rc;

	if (!no_fw_load) {
		wil_info(wil, "Use firmware <%s>\n", WIL_FW_NAME);
		wil_halt_cpu(wil);
		/* Loading f/w from the file */
		rc = wil_request_firmware(wil, WIL_FW_NAME);
		if (rc)
			return rc;

		/* clear any interrupts which on-card-firmware may have set */
		wil6210_clear_irq(wil);
		{ /* CAF_ICR - clear and mask */
			u32 a = HOSTADDR(RGF_CAF_ICR) +
				offsetof(struct RGF_ICR, ICR);
			u32 m = HOSTADDR(RGF_CAF_ICR) +
				offsetof(struct RGF_ICR, IMV);
			u32 icr = ioread32(wil->csr + a);

			iowrite32(icr, wil->csr + a); /* W1C */
			iowrite32(~0, wil->csr + m);
			wmb(); /* wait for completion */
		}
		wil_release_cpu(wil);
	} else {
		wil_info(wil, "Use firmware from on-card flash\n");
	}

	/* init after reset */
	wil->pending_connect_cid = -1;
	reinit_completion(&wil->wmi_ready);
	reinit_completion(&wil->wmi_call);

	wil_unmask_irq(wil);

	/* we just started MAC, wait for FW ready */
	rc = wil_wait_for_fw_ready(wil);

	return rc;
}

void wil_fw_error_recovery(struct wil6210_priv *wil)
{
	wil_dbg_misc(wil, "starting fw error recovery\n");
	wil->recovery_state = fw_recovery_pending;
	schedule_work(&wil->fw_error_worker);
}

void wil_link_on(struct wil6210_priv *wil)
{
	struct net_device *ndev = wil_to_ndev(wil);

	wil_dbg_misc(wil, "%s()\n", __func__);

	netif_carrier_on(ndev);
	wil_dbg_misc(wil, "netif_tx_wake : link on\n");
	netif_tx_wake_all_queues(ndev);
}

void wil_link_off(struct wil6210_priv *wil)
{
	struct net_device *ndev = wil_to_ndev(wil);

	wil_dbg_misc(wil, "%s()\n", __func__);

	netif_tx_stop_all_queues(ndev);
	wil_dbg_misc(wil, "netif_tx_stop : link off\n");
	netif_carrier_off(ndev);
}

int __wil_up(struct wil6210_priv *wil)
{
	struct net_device *ndev = wil_to_ndev(wil);
	struct wireless_dev *wdev = wil->wdev;
	int rc;

	WARN_ON(!mutex_is_locked(&wil->mutex));

	rc = wil_reset(wil);
	if (rc)
		return rc;

	/* Rx VRING. After MAC and beacon */
	rc = wil_rx_init(wil);
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
	set_bit(wil_status_napi_en, &wil->status);

	if (wil->platform_ops.bus_request)
		wil->platform_ops.bus_request(wil->platform_handle,
					      WIL_MAX_BUS_REQUEST_KBPS);

	return 0;
}

int wil_up(struct wil6210_priv *wil)
{
	int rc;

	wil_dbg_misc(wil, "%s()\n", __func__);

	mutex_lock(&wil->mutex);
	rc = __wil_up(wil);
	mutex_unlock(&wil->mutex);

	return rc;
}

int __wil_down(struct wil6210_priv *wil)
{
	int iter = WAIT_FOR_DISCONNECT_TIMEOUT_MS /
			WAIT_FOR_DISCONNECT_INTERVAL_MS;

	WARN_ON(!mutex_is_locked(&wil->mutex));

	if (wil->platform_ops.bus_request)
		wil->platform_ops.bus_request(wil->platform_handle, 0);

	wil_disable_irq(wil);
	if (test_and_clear_bit(wil_status_napi_en, &wil->status)) {
		napi_disable(&wil->napi_rx);
		napi_disable(&wil->napi_tx);
		wil_dbg_misc(wil, "NAPI disable\n");
	}
	wil_enable_irq(wil);

	if (wil->scan_request) {
		wil_dbg_misc(wil, "Abort scan_request 0x%p\n",
			     wil->scan_request);
		del_timer_sync(&wil->scan_timer);
		cfg80211_scan_done(wil->scan_request, true);
		wil->scan_request = NULL;
	}

	if (test_bit(wil_status_fwconnected, &wil->status) ||
	    test_bit(wil_status_fwconnecting, &wil->status))
		wmi_send(wil, WMI_DISCONNECT_CMDID, NULL, 0);

	/* make sure wil is idle (not connected) */
	mutex_unlock(&wil->mutex);
	while (iter--) {
		int idle = !test_bit(wil_status_fwconnected, &wil->status) &&
			   !test_bit(wil_status_fwconnecting, &wil->status);
		if (idle)
			break;
		msleep(WAIT_FOR_DISCONNECT_INTERVAL_MS);
	}
	mutex_lock(&wil->mutex);

	if (!iter)
		wil_err(wil, "timeout waiting for idle FW/HW\n");

	wil_rx_fini(wil);

	return 0;
}

int wil_down(struct wil6210_priv *wil)
{
	int rc;

	wil_dbg_misc(wil, "%s()\n", __func__);

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
