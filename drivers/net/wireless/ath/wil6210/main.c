/*
 * Copyright (c) 2012-2015 Qualcomm Atheros, Inc.
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

/* if not set via modparam, will be set to default value of 1/8 of
 * rx ring size during init flow
 */
unsigned short rx_ring_overflow_thrsh = WIL6210_RX_HIGH_TRSH_INIT;
module_param(rx_ring_overflow_thrsh, ushort, S_IRUGO);
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

static struct kernel_param_ops mtu_max_ops = {
	.set = mtu_max_set,
	.get = param_get_uint,
};

module_param_cb(mtu_max, &mtu_max_ops, &mtu_max, S_IRUGO);
MODULE_PARM_DESC(mtu_max, " Max MTU value.");

static uint rx_ring_order = WIL_RX_RING_SIZE_ORDER_DEFAULT;
static uint tx_ring_order = WIL_TX_RING_SIZE_ORDER_DEFAULT;

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

static struct kernel_param_ops ring_order_ops = {
	.set = ring_order_set,
	.get = param_get_uint,
};

module_param_cb(rx_ring_order, &ring_order_ops, &rx_ring_order, S_IRUGO);
MODULE_PARM_DESC(rx_ring_order, " Rx ring order; size = 1 << order");
module_param_cb(tx_ring_order, &ring_order_ops, &tx_ring_order, S_IRUGO);
MODULE_PARM_DESC(tx_ring_order, " Tx ring order; size = 1 << order");

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

static void wil_disconnect_cid(struct wil6210_priv *wil, int cid,
			       u16 reason_code, bool from_event)
__acquires(&sta->tid_rx_lock) __releases(&sta->tid_rx_lock)
{
	uint i;
	struct net_device *ndev = wil_to_ndev(wil);
	struct wireless_dev *wdev = wil->wdev;
	struct wil_sta_info *sta = &wil->sta[cid];

	might_sleep();
	wil_dbg_misc(wil, "%s(CID %d, status %d)\n", __func__, cid,
		     sta->status);

	sta->data_port_open = false;
	if (sta->status != wil_sta_unused) {
		if (!from_event)
			wmi_disconnect_sta(wil, sta->addr, reason_code);

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

		spin_lock_bh(&sta->tid_rx_lock);

		r = sta->tid_rx[i];
		sta->tid_rx[i] = NULL;
		wil_tid_ampdu_rx_free(wil, r);

		spin_unlock_bh(&sta->tid_rx_lock);
	}
	for (i = 0; i < ARRAY_SIZE(wil->vring_tx); i++) {
		if (wil->vring2cid_tid[i][0] == cid)
			wil_vring_fini_tx(wil, i);
	}
	memset(&sta->stats, 0, sizeof(sta->stats));
}

static void _wil6210_disconnect(struct wil6210_priv *wil, const u8 *bssid,
				u16 reason_code, bool from_event)
{
	int cid = -ENOENT;
	struct net_device *ndev = wil_to_ndev(wil);
	struct wireless_dev *wdev = wil->wdev;

	might_sleep();
	wil_dbg_misc(wil, "%s(bssid=%pM, reason=%d, ev%s)\n", __func__, bssid,
		     reason_code, from_event ? "+" : "-");

	/* Cases are:
	 * - disconnect single STA, still connected
	 * - disconnect single STA, already disconnected
	 * - disconnect all
	 *
	 * For "disconnect all", there are 2 options:
	 * - bssid == NULL
	 * - bssid is our MAC address
	 */
	if (bssid && memcmp(ndev->dev_addr, bssid, ETH_ALEN)) {
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
		netif_tx_stop_all_queues(ndev);
		netif_carrier_off(ndev);

		if (test_bit(wil_status_fwconnected, wil->status)) {
			clear_bit(wil_status_fwconnected, wil->status);
			cfg80211_disconnected(ndev, reason_code,
					      NULL, 0, GFP_KERNEL);
		} else if (test_bit(wil_status_fwconnecting, wil->status)) {
			cfg80211_connect_result(ndev, bssid, NULL, 0, NULL, 0,
						WLAN_STATUS_UNSPECIFIED_FAILURE,
						GFP_KERNEL);
		}
		clear_bit(wil_status_fwconnecting, wil->status);
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
	_wil6210_disconnect(wil, NULL, WLAN_REASON_UNSPECIFIED, false);
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

	clear_bit(wil_status_fwready, wil->status);
	wil_err(wil, "Scan timeout detected, start fw error recovery\n");
	wil->recovery_state = fw_recovery_pending;
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

	if (!netif_running(wil_to_ndev(wil))) {
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

static void wil_connect_worker(struct work_struct *work)
{
	int rc;
	struct wil6210_priv *wil = container_of(work, struct wil6210_priv,
						connect_worker);
	struct net_device *ndev = wil_to_ndev(wil);

	int cid = wil->pending_connect_cid;
	int ringid = wil_find_free_vring(wil);

	if (cid < 0) {
		wil_err(wil, "No connection pending\n");
		return;
	}

	wil_dbg_wmi(wil, "Configure for connection CID %d\n", cid);

	rc = wil_vring_init_tx(wil, ringid, 1 << tx_ring_order, cid, 0);
	wil->pending_connect_cid = -1;
	if (rc == 0) {
		wil->sta[cid].status = wil_sta_connected;
		netif_tx_wake_all_queues(ndev);
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
	mutex_init(&wil->back_rx_mutex);
	mutex_init(&wil->back_tx_mutex);
	mutex_init(&wil->probe_client_mutex);

	init_completion(&wil->wmi_ready);
	init_completion(&wil->wmi_call);

	wil->pending_connect_cid = -1;
	setup_timer(&wil->connect_timer, wil_connect_timer_fn, (ulong)wil);
	setup_timer(&wil->scan_timer, wil_scan_timer_fn, (ulong)wil);

	INIT_WORK(&wil->connect_worker, wil_connect_worker);
	INIT_WORK(&wil->disconnect_worker, wil_disconnect_worker);
	INIT_WORK(&wil->wmi_event_worker, wmi_event_worker);
	INIT_WORK(&wil->fw_error_worker, wil_fw_error_worker);
	INIT_WORK(&wil->back_rx_worker, wil_back_rx_worker);
	INIT_WORK(&wil->back_tx_worker, wil_back_tx_worker);
	INIT_WORK(&wil->probe_client_worker, wil_probe_client_worker);

	INIT_LIST_HEAD(&wil->pending_wmi_ev);
	INIT_LIST_HEAD(&wil->back_rx_pending);
	INIT_LIST_HEAD(&wil->back_tx_pending);
	INIT_LIST_HEAD(&wil->probe_client_pending);
	spin_lock_init(&wil->wmi_ev_lock);
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
	return 0;

out_wmi_wq:
	destroy_workqueue(wil->wmi_wq);

	return -EAGAIN;
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
	wil_dbg_misc(wil, "%s()\n", __func__);

	del_timer_sync(&wil->connect_timer);
	_wil6210_disconnect(wil, bssid, reason_code, from_event);
}

void wil_priv_deinit(struct wil6210_priv *wil)
{
	wil_dbg_misc(wil, "%s()\n", __func__);

	wil_set_recovery_state(wil, fw_recovery_idle);
	del_timer_sync(&wil->scan_timer);
	cancel_work_sync(&wil->disconnect_worker);
	cancel_work_sync(&wil->fw_error_worker);
	mutex_lock(&wil->mutex);
	wil6210_disconnect(wil, NULL, WLAN_REASON_DEAUTH_LEAVING, false);
	mutex_unlock(&wil->mutex);
	wmi_event_flush(wil);
	wil_back_rx_flush(wil);
	cancel_work_sync(&wil->back_rx_worker);
	wil_back_tx_flush(wil);
	cancel_work_sync(&wil->back_tx_worker);
	wil_probe_client_flush(wil);
	cancel_work_sync(&wil->probe_client_worker);
	destroy_workqueue(wil->wq_service);
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
	u32 x;

	wil_dbg_misc(wil, "Resetting \"%s\"...\n", wil->hw_name);

	/* Clear MAC link up */
	S(RGF_HP_CTRL, BIT(15));
	S(RGF_USER_CLKS_CTL_SW_RST_MASK_0, BIT_HPAL_PERST_FROM_PAD);
	S(RGF_USER_CLKS_CTL_SW_RST_MASK_0, BIT_CAR_PERST_RST);

	wil_halt_cpu(wil);

	/* clear all boot loader "ready" bits */
	W(RGF_USER_BL + offsetof(struct RGF_BL, ready), 0);
	/* Clear Fw Download notification */
	C(RGF_USER_USAGE_6, BIT(0));

	S(RGF_CAF_OSC_CONTROL, BIT_CAF_OSC_XTAL_EN);
	/* XTAL stabilization should take about 3ms */
	usleep_range(5000, 7000);
	x = R(RGF_CAF_PLL_LOCK_STATUS);
	if (!(x & BIT_CAF_OSC_DIG_XTAL_STABLE)) {
		wil_err(wil, "Xtal stabilization timeout\n"
			"RGF_CAF_PLL_LOCK_STATUS = 0x%08x\n", x);
		return -ETIME;
	}
	/* switch 10k to XTAL*/
	C(RGF_USER_SPARROW_M_4, BIT_SPARROW_M_4_SEL_SLEEP_OR_REF);
	/* 40 MHz */
	C(RGF_USER_CLKS_CTL_0, BIT_USER_CLKS_CAR_AHB_SW_SEL);

	W(RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_0, 0x3ff81f);
	W(RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_1, 0xf);

	W(RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0xFE000000);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_1, 0x0000003F);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_3, 0x000000f0);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_0, 0xFFE7FE00);

	W(RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_0, 0x0);
	W(RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_1, 0x0);

	W(RGF_USER_CLKS_CTL_SW_RST_VEC_3, 0);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_1, 0);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_0, 0);

	W(RGF_USER_CLKS_CTL_SW_RST_VEC_3, 0x00000003);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0x00008000); /* reset A2 PCIE AHB */

	W(RGF_USER_CLKS_CTL_SW_RST_VEC_0, 0);

	/* wait until device ready. typical time is 20..80 msec */
	do {
		msleep(RST_DELAY);
		x = R(RGF_USER_BL + offsetof(struct RGF_BL, ready));
		if (delay++ > RST_COUNT) {
			wil_err(wil, "Reset not completed, bl.ready 0x%08x\n",
				x);
			return -ETIME;
		}
	} while (!(x & BIT_BL_READY));

	C(RGF_USER_CLKS_CTL_0, BIT_USER_CLKS_RST_PWGD);

	/* enable fix for HW bug related to the SA/DA swap in AP Rx */
	S(RGF_DMA_OFUL_NID_0, BIT_DMA_OFUL_NID_0_RX_EXT_TR_EN |
	  BIT_DMA_OFUL_NID_0_RX_EXT_A3_SRC);

	wil_dbg_misc(wil, "Reset completed in %d ms\n", delay * RST_DELAY);
	return 0;
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
	struct RGF_BL bl;

	wil_memcpy_fromio_32(&bl, wil->csr + HOSTADDR(RGF_USER_BL), sizeof(bl));
	le32_to_cpus(&bl.ready);
	le32_to_cpus(&bl.version);
	le32_to_cpus(&bl.rf_type);
	le32_to_cpus(&bl.baseband_type);

	if (!is_valid_ether_addr(bl.mac_address)) {
		wil_err(wil, "BL: Invalid MAC %pM\n", bl.mac_address);
		return -EINVAL;
	}

	ether_addr_copy(ndev->perm_addr, bl.mac_address);
	if (!is_valid_ether_addr(ndev->dev_addr))
		ether_addr_copy(ndev->dev_addr, bl.mac_address);
	wil_info(wil,
		 "Boot Loader: ver = %d MAC = %pM RF = 0x%08x bband = 0x%08x\n",
		 bl.version, bl.mac_address, bl.rf_type, bl.baseband_type);

	return 0;
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
int wil_reset(struct wil6210_priv *wil, bool load_fw)
{
	int rc;

	wil_dbg_misc(wil, "%s()\n", __func__);

	if (wil->hw_version == HW_VER_UNKNOWN)
		return -ENODEV;

	WARN_ON(!mutex_is_locked(&wil->mutex));
	WARN_ON(test_bit(wil_status_napi_en, wil->status));

	cancel_work_sync(&wil->disconnect_worker);
	wil6210_disconnect(wil, NULL, WLAN_REASON_DEAUTH_LEAVING, false);

	/* prevent NAPI from being scheduled */
	bitmap_zero(wil->status, wil_status_last);

	if (wil->scan_request) {
		wil_dbg_misc(wil, "Abort scan_request 0x%p\n",
			     wil->scan_request);
		del_timer_sync(&wil->scan_timer);
		cfg80211_scan_done(wil->scan_request, true);
		wil->scan_request = NULL;
	}

	wil_mask_irq(wil);

	wmi_event_flush(wil);

	flush_workqueue(wil->wq_service);
	flush_workqueue(wil->wmi_wq);

	rc = wil_target_reset(wil);
	wil_rx_fini(wil);
	if (rc)
		return rc;

	rc = wil_get_bl_info(wil);
	if (rc)
		return rc;

	if (load_fw) {
		wil_info(wil, "Use firmware <%s> + board <%s>\n", WIL_FW_NAME,
			 WIL_FW2_NAME);

		wil_halt_cpu(wil);
		/* Loading f/w from the file */
		rc = wil_request_firmware(wil, WIL_FW_NAME);
		if (rc)
			return rc;
		rc = wil_request_firmware(wil, WIL_FW2_NAME);
		if (rc)
			return rc;

		/* Mark FW as loaded from host */
		S(RGF_USER_USAGE_6, 1);

		/* clear any interrupts which on-card-firmware
		 * may have set
		 */
		wil6210_clear_irq(wil);
		/* CAF_ICR - clear and mask */
		/* it is W1C, clear by writing back same value */
		S(RGF_CAF_ICR + offsetof(struct RGF_ICR, ICR), 0);
		W(RGF_CAF_ICR + offsetof(struct RGF_ICR, IMV), ~0);

		wil_release_cpu(wil);
	}

	/* init after reset */
	wil->pending_connect_cid = -1;
	reinit_completion(&wil->wmi_ready);
	reinit_completion(&wil->wmi_call);

	if (load_fw) {
		wil_configure_interrupt_moderation(wil);
		wil_unmask_irq(wil);

		/* we just started MAC, wait for FW ready */
		rc = wil_wait_for_fw_ready(wil);
	}

	return rc;
}

#undef R
#undef W
#undef S
#undef C

void wil_fw_error_recovery(struct wil6210_priv *wil)
{
	wil_dbg_misc(wil, "starting fw error recovery\n");
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
	if (test_and_clear_bit(wil_status_napi_en, wil->status)) {
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

	if (test_bit(wil_status_fwconnected, wil->status) ||
	    test_bit(wil_status_fwconnecting, wil->status))
		wmi_send(wil, WMI_DISCONNECT_CMDID, NULL, 0);

	/* make sure wil is idle (not connected) */
	mutex_unlock(&wil->mutex);
	while (iter--) {
		int idle = !test_bit(wil_status_fwconnected, wil->status) &&
			   !test_bit(wil_status_fwconnecting, wil->status);
		if (idle)
			break;
		msleep(WAIT_FOR_DISCONNECT_INTERVAL_MS);
	}
	mutex_lock(&wil->mutex);

	if (!iter)
		wil_err(wil, "timeout waiting for idle FW/HW\n");

	wil_reset(wil, false);

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
