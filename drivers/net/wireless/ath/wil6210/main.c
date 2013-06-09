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

#include <linux/moduleparam.h>
#include <linux/if_arp.h>

#include "wil6210.h"

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

static void _wil6210_disconnect(struct wil6210_priv *wil, void *bssid)
{
	uint i;
	struct net_device *ndev = wil_to_ndev(wil);

	wil_dbg_misc(wil, "%s()\n", __func__);

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
	for (i = 0; i < ARRAY_SIZE(wil->vring_tx); i++)
		wil_vring_fini_tx(wil, i);

	clear_bit(wil_status_dontscan, &wil->status);
}

static void wil_disconnect_worker(struct work_struct *work)
{
	struct wil6210_priv *wil = container_of(work,
			struct wil6210_priv, disconnect_worker);

	_wil6210_disconnect(wil, NULL);
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

static void wil_connect_worker(struct work_struct *work)
{
	int rc;
	struct wil6210_priv *wil = container_of(work, struct wil6210_priv,
						connect_worker);
	int cid = wil->pending_connect_cid;

	if (cid < 0) {
		wil_err(wil, "No connection pending\n");
		return;
	}

	wil_dbg_wmi(wil, "Configure for connection CID %d\n", cid);

	rc = wil_vring_init_tx(wil, 0, WIL6210_TX_RING_SIZE, cid, 0);
	wil->pending_connect_cid = -1;
	if (rc == 0)
		wil_link_on(wil);
}

int wil_priv_init(struct wil6210_priv *wil)
{
	wil_dbg_misc(wil, "%s()\n", __func__);

	mutex_init(&wil->mutex);
	mutex_init(&wil->wmi_mutex);

	init_completion(&wil->wmi_ready);

	wil->pending_connect_cid = -1;
	setup_timer(&wil->connect_timer, wil_connect_timer_fn, (ulong)wil);

	INIT_WORK(&wil->connect_worker, wil_connect_worker);
	INIT_WORK(&wil->disconnect_worker, wil_disconnect_worker);
	INIT_WORK(&wil->wmi_event_worker, wmi_event_worker);

	INIT_LIST_HEAD(&wil->pending_wmi_ev);
	spin_lock_init(&wil->wmi_ev_lock);

	wil->wmi_wq = create_singlethread_workqueue(WIL_NAME"_wmi");
	if (!wil->wmi_wq)
		return -EAGAIN;

	wil->wmi_wq_conn = create_singlethread_workqueue(WIL_NAME"_connect");
	if (!wil->wmi_wq_conn) {
		destroy_workqueue(wil->wmi_wq);
		return -EAGAIN;
	}

	return 0;
}

void wil6210_disconnect(struct wil6210_priv *wil, void *bssid)
{
	del_timer_sync(&wil->connect_timer);
	_wil6210_disconnect(wil, bssid);
}

void wil_priv_deinit(struct wil6210_priv *wil)
{
	cancel_work_sync(&wil->disconnect_worker);
	wil6210_disconnect(wil, NULL);
	wmi_event_flush(wil);
	destroy_workqueue(wil->wmi_wq_conn);
	destroy_workqueue(wil->wmi_wq);
}

static void wil_target_reset(struct wil6210_priv *wil)
{
	wil_dbg_misc(wil, "Resetting...\n");

	/* register write */
#define W(a, v) iowrite32(v, wil->csr + HOSTADDR(a))
	/* register set = read, OR, write */
#define S(a, v) iowrite32(ioread32(wil->csr + HOSTADDR(a)) | v, \
		wil->csr + HOSTADDR(a))

	/* hpal_perst_from_pad_src_n_mask */
	S(RGF_USER_CLKS_CTL_SW_RST_MASK_0, BIT(6));
	/* car_perst_rst_src_n_mask */
	S(RGF_USER_CLKS_CTL_SW_RST_MASK_0, BIT(7));

	W(RGF_USER_MAC_CPU_0,  BIT(1)); /* mac_cpu_man_rst */
	W(RGF_USER_USER_CPU_0, BIT(1)); /* user_cpu_man_rst */

	W(RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0xFE000000);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_1, 0x0000003F);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_3, 0x00000170);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_0, 0xFFE7FC00);

	W(RGF_USER_CLKS_CTL_SW_RST_VEC_3, 0);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_1, 0);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_0, 0);

	W(RGF_USER_CLKS_CTL_SW_RST_VEC_3, 0x00000001);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_2, 0x00000080);
	W(RGF_USER_CLKS_CTL_SW_RST_VEC_0, 0);

	wil_dbg_misc(wil, "Reset completed\n");

#undef W
#undef S
}

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
		wil_dbg_misc(wil, "FW ready after %d ms\n",
			     jiffies_to_msecs(to-left));
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

	cancel_work_sync(&wil->disconnect_worker);
	wil6210_disconnect(wil, NULL);

	wil6210_disable_irq(wil);
	wil->status = 0;

	wmi_event_flush(wil);

	flush_workqueue(wil->wmi_wq_conn);
	flush_workqueue(wil->wmi_wq);

	/* TODO: put MAC in reset */
	wil_target_reset(wil);

	/* init after reset */
	wil->pending_connect_cid = -1;
	INIT_COMPLETION(wil->wmi_ready);

	/* TODO: release MAC reset */
	wil6210_enable_irq(wil);

	/* we just started MAC, wait for FW ready */
	rc = wil_wait_for_fw_ready(wil);

	return rc;
}


void wil_link_on(struct wil6210_priv *wil)
{
	struct net_device *ndev = wil_to_ndev(wil);

	wil_dbg_misc(wil, "%s()\n", __func__);

	netif_carrier_on(ndev);
	netif_tx_wake_all_queues(ndev);
}

void wil_link_off(struct wil6210_priv *wil)
{
	struct net_device *ndev = wil_to_ndev(wil);

	wil_dbg_misc(wil, "%s()\n", __func__);

	netif_tx_stop_all_queues(ndev);
	netif_carrier_off(ndev);
}

static int __wil_up(struct wil6210_priv *wil)
{
	struct net_device *ndev = wil_to_ndev(wil);
	struct wireless_dev *wdev = wil->wdev;
	int rc;

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


	napi_enable(&wil->napi_rx);
	napi_enable(&wil->napi_tx);

	return 0;
}

int wil_up(struct wil6210_priv *wil)
{
	int rc;

	mutex_lock(&wil->mutex);
	rc = __wil_up(wil);
	mutex_unlock(&wil->mutex);

	return rc;
}

static int __wil_down(struct wil6210_priv *wil)
{
	napi_disable(&wil->napi_rx);
	napi_disable(&wil->napi_tx);

	if (wil->scan_request) {
		cfg80211_scan_done(wil->scan_request, true);
		wil->scan_request = NULL;
	}

	wil6210_disconnect(wil, NULL);
	wil_rx_fini(wil);

	return 0;
}

int wil_down(struct wil6210_priv *wil)
{
	int rc;

	mutex_lock(&wil->mutex);
	rc = __wil_down(wil);
	mutex_unlock(&wil->mutex);

	return rc;
}
