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
#include <linux/etherdevice.h>
#include <linux/if_arp.h>

#include "wil6210.h"
#include "txrx.h"
#include "wmi.h"
#include "trace.h"

static uint max_assoc_sta = WIL6210_MAX_CID;
module_param(max_assoc_sta, uint, 0644);
MODULE_PARM_DESC(max_assoc_sta, " Max number of stations associated to the AP");

int agg_wsize; /* = 0; */
module_param(agg_wsize, int, 0644);
MODULE_PARM_DESC(agg_wsize, " Window size for Tx Block Ack after connect;"
		 " 0 - use default; < 0 - don't auto-establish");

u8 led_id = WIL_LED_INVALID_ID;
module_param(led_id, byte, 0444);
MODULE_PARM_DESC(led_id,
		 " 60G device led enablement. Set the led ID (0-2) to enable");

/**
 * WMI event receiving - theory of operations
 *
 * When firmware about to report WMI event, it fills memory area
 * in the mailbox and raises misc. IRQ. Thread interrupt handler invoked for
 * the misc IRQ, function @wmi_recv_cmd called by thread IRQ handler.
 *
 * @wmi_recv_cmd reads event, allocates memory chunk  and attaches it to the
 * event list @wil->pending_wmi_ev. Then, work queue @wil->wmi_wq wakes up
 * and handles events within the @wmi_event_worker. Every event get detached
 * from list, processed and deleted.
 *
 * Purpose for this mechanism is to release IRQ thread; otherwise,
 * if WMI event handling involves another WMI command flow, this 2-nd flow
 * won't be completed because of blocked IRQ thread.
 */

/**
 * Addressing - theory of operations
 *
 * There are several buses present on the WIL6210 card.
 * Same memory areas are visible at different address on
 * the different busses. There are 3 main bus masters:
 *  - MAC CPU (ucode)
 *  - User CPU (firmware)
 *  - AHB (host)
 *
 * On the PCI bus, there is one BAR (BAR0) of 2Mb size, exposing
 * AHB addresses starting from 0x880000
 *
 * Internally, firmware uses addresses that allows faster access but
 * are invisible from the host. To read from these addresses, alternative
 * AHB address must be used.
 *
 * Memory mapping
 * Linker address         PCI/Host address
 *                        0x880000 .. 0xa80000  2Mb BAR0
 * 0x800000 .. 0x807000   0x900000 .. 0x907000  28k DCCM
 * 0x840000 .. 0x857000   0x908000 .. 0x91f000  92k PERIPH
 */

/**
 * @fw_mapping provides memory remapping table
 *
 * array size should be in sync with the declaration in the wil6210.h
 */
const struct fw_map fw_mapping[] = {
	/* FW code RAM 256k */
	{0x000000, 0x040000, 0x8c0000, "fw_code", true},
	/* FW data RAM 32k */
	{0x800000, 0x808000, 0x900000, "fw_data", true},
	/* periph data 128k */
	{0x840000, 0x860000, 0x908000, "fw_peri", true},
	/* various RGF 40k */
	{0x880000, 0x88a000, 0x880000, "rgf", true},
	/* AGC table   4k */
	{0x88a000, 0x88b000, 0x88a000, "AGC_tbl", true},
	/* Pcie_ext_rgf 4k */
	{0x88b000, 0x88c000, 0x88b000, "rgf_ext", true},
	/* mac_ext_rgf 512b */
	{0x88c000, 0x88c200, 0x88c000, "mac_rgf_ext", true},
	/* upper area 548k */
	{0x8c0000, 0x949000, 0x8c0000, "upper", true},
	/* UCODE areas - accessible by debugfs blobs but not by
	 * wmi_addr_remap. UCODE areas MUST be added AFTER FW areas!
	 */
	/* ucode code RAM 128k */
	{0x000000, 0x020000, 0x920000, "uc_code", false},
	/* ucode data RAM 16k */
	{0x800000, 0x804000, 0x940000, "uc_data", false},
};

struct blink_on_off_time led_blink_time[] = {
	{WIL_LED_BLINK_ON_SLOW_MS, WIL_LED_BLINK_OFF_SLOW_MS},
	{WIL_LED_BLINK_ON_MED_MS, WIL_LED_BLINK_OFF_MED_MS},
	{WIL_LED_BLINK_ON_FAST_MS, WIL_LED_BLINK_OFF_FAST_MS},
};

u8 led_polarity = LED_POLARITY_LOW_ACTIVE;

/**
 * return AHB address for given firmware internal (linker) address
 * @x - internal address
 * If address have no valid AHB mapping, return 0
 */
static u32 wmi_addr_remap(u32 x)
{
	uint i;

	for (i = 0; i < ARRAY_SIZE(fw_mapping); i++) {
		if (fw_mapping[i].fw &&
		    ((x >= fw_mapping[i].from) && (x < fw_mapping[i].to)))
			return x + fw_mapping[i].host - fw_mapping[i].from;
	}

	return 0;
}

/**
 * Check address validity for WMI buffer; remap if needed
 * @ptr - internal (linker) fw/ucode address
 *
 * Valid buffer should be DWORD aligned
 *
 * return address for accessing buffer from the host;
 * if buffer is not valid, return NULL.
 */
void __iomem *wmi_buffer(struct wil6210_priv *wil, __le32 ptr_)
{
	u32 off;
	u32 ptr = le32_to_cpu(ptr_);

	if (ptr % 4)
		return NULL;

	ptr = wmi_addr_remap(ptr);
	if (ptr < WIL6210_FW_HOST_OFF)
		return NULL;

	off = HOSTADDR(ptr);
	if (off > WIL6210_MEM_SIZE - 4)
		return NULL;

	return wil->csr + off;
}

/**
 * Check address validity
 */
void __iomem *wmi_addr(struct wil6210_priv *wil, u32 ptr)
{
	u32 off;

	if (ptr % 4)
		return NULL;

	if (ptr < WIL6210_FW_HOST_OFF)
		return NULL;

	off = HOSTADDR(ptr);
	if (off > WIL6210_MEM_SIZE - 4)
		return NULL;

	return wil->csr + off;
}

int wmi_read_hdr(struct wil6210_priv *wil, __le32 ptr,
		 struct wil6210_mbox_hdr *hdr)
{
	void __iomem *src = wmi_buffer(wil, ptr);

	if (!src)
		return -EINVAL;

	wil_memcpy_fromio_32(hdr, src, sizeof(*hdr));

	return 0;
}

static int __wmi_send(struct wil6210_priv *wil, u16 cmdid, void *buf, u16 len)
{
	struct {
		struct wil6210_mbox_hdr hdr;
		struct wmi_cmd_hdr wmi;
	} __packed cmd = {
		.hdr = {
			.type = WIL_MBOX_HDR_TYPE_WMI,
			.flags = 0,
			.len = cpu_to_le16(sizeof(cmd.wmi) + len),
		},
		.wmi = {
			.mid = 0,
			.command_id = cpu_to_le16(cmdid),
		},
	};
	struct wil6210_mbox_ring *r = &wil->mbox_ctl.tx;
	struct wil6210_mbox_ring_desc d_head;
	u32 next_head;
	void __iomem *dst;
	void __iomem *head = wmi_addr(wil, r->head);
	uint retry;
	int rc = 0;

	if (sizeof(cmd) + len > r->entry_size) {
		wil_err(wil, "WMI size too large: %d bytes, max is %d\n",
			(int)(sizeof(cmd) + len), r->entry_size);
		return -ERANGE;
	}

	might_sleep();

	if (!test_bit(wil_status_fwready, wil->status)) {
		wil_err(wil, "WMI: cannot send command while FW not ready\n");
		return -EAGAIN;
	}

	if (!head) {
		wil_err(wil, "WMI head is garbage: 0x%08x\n", r->head);
		return -EINVAL;
	}

	wil_halp_vote(wil);

	/* read Tx head till it is not busy */
	for (retry = 5; retry > 0; retry--) {
		wil_memcpy_fromio_32(&d_head, head, sizeof(d_head));
		if (d_head.sync == 0)
			break;
		msleep(20);
	}
	if (d_head.sync != 0) {
		wil_err(wil, "WMI head busy\n");
		rc = -EBUSY;
		goto out;
	}
	/* next head */
	next_head = r->base + ((r->head - r->base + sizeof(d_head)) % r->size);
	wil_dbg_wmi(wil, "Head 0x%08x -> 0x%08x\n", r->head, next_head);
	/* wait till FW finish with previous command */
	for (retry = 5; retry > 0; retry--) {
		if (!test_bit(wil_status_fwready, wil->status)) {
			wil_err(wil, "WMI: cannot send command while FW not ready\n");
			rc = -EAGAIN;
			goto out;
		}
		r->tail = wil_r(wil, RGF_MBOX +
				offsetof(struct wil6210_mbox_ctl, tx.tail));
		if (next_head != r->tail)
			break;
		msleep(20);
	}
	if (next_head == r->tail) {
		wil_err(wil, "WMI ring full\n");
		rc = -EBUSY;
		goto out;
	}
	dst = wmi_buffer(wil, d_head.addr);
	if (!dst) {
		wil_err(wil, "invalid WMI buffer: 0x%08x\n",
			le32_to_cpu(d_head.addr));
		rc = -EAGAIN;
		goto out;
	}
	cmd.hdr.seq = cpu_to_le16(++wil->wmi_seq);
	/* set command */
	wil_dbg_wmi(wil, "WMI command 0x%04x [%d]\n", cmdid, len);
	wil_hex_dump_wmi("Cmd ", DUMP_PREFIX_OFFSET, 16, 1, &cmd,
			 sizeof(cmd), true);
	wil_hex_dump_wmi("cmd ", DUMP_PREFIX_OFFSET, 16, 1, buf,
			 len, true);
	wil_memcpy_toio_32(dst, &cmd, sizeof(cmd));
	wil_memcpy_toio_32(dst + sizeof(cmd), buf, len);
	/* mark entry as full */
	wil_w(wil, r->head + offsetof(struct wil6210_mbox_ring_desc, sync), 1);
	/* advance next ptr */
	wil_w(wil, RGF_MBOX + offsetof(struct wil6210_mbox_ctl, tx.head),
	      r->head = next_head);

	trace_wil6210_wmi_cmd(&cmd.wmi, buf, len);

	/* interrupt to FW */
	wil_w(wil, RGF_USER_USER_ICR + offsetof(struct RGF_ICR, ICS),
	      SW_INT_MBOX);

out:
	wil_halp_unvote(wil);
	return rc;
}

int wmi_send(struct wil6210_priv *wil, u16 cmdid, void *buf, u16 len)
{
	int rc;

	mutex_lock(&wil->wmi_mutex);
	rc = __wmi_send(wil, cmdid, buf, len);
	mutex_unlock(&wil->wmi_mutex);

	return rc;
}

/*=== Event handlers ===*/
static void wmi_evt_ready(struct wil6210_priv *wil, int id, void *d, int len)
{
	struct wireless_dev *wdev = wil->wdev;
	struct wmi_ready_event *evt = d;

	wil->n_mids = evt->numof_additional_mids;

	wil_info(wil, "FW ver. %s(SW %d); MAC %pM; %d MID's\n",
		 wil->fw_version, le32_to_cpu(evt->sw_version),
		 evt->mac, wil->n_mids);
	/* ignore MAC address, we already have it from the boot loader */
	strlcpy(wdev->wiphy->fw_version, wil->fw_version,
		sizeof(wdev->wiphy->fw_version));

	wil_set_recovery_state(wil, fw_recovery_idle);
	set_bit(wil_status_fwready, wil->status);
	/* let the reset sequence continue */
	complete(&wil->wmi_ready);
}

static void wmi_evt_rx_mgmt(struct wil6210_priv *wil, int id, void *d, int len)
{
	struct wmi_rx_mgmt_packet_event *data = d;
	struct wiphy *wiphy = wil_to_wiphy(wil);
	struct ieee80211_mgmt *rx_mgmt_frame =
			(struct ieee80211_mgmt *)data->payload;
	int flen = len - offsetof(struct wmi_rx_mgmt_packet_event, payload);
	int ch_no;
	u32 freq;
	struct ieee80211_channel *channel;
	s32 signal;
	__le16 fc;
	u32 d_len;
	u16 d_status;

	if (flen < 0) {
		wil_err(wil, "MGMT Rx: short event, len %d\n", len);
		return;
	}

	d_len = le32_to_cpu(data->info.len);
	if (d_len != flen) {
		wil_err(wil,
			"MGMT Rx: length mismatch, d_len %d should be %d\n",
			d_len, flen);
		return;
	}

	ch_no = data->info.channel + 1;
	freq = ieee80211_channel_to_frequency(ch_no, NL80211_BAND_60GHZ);
	channel = ieee80211_get_channel(wiphy, freq);
	signal = data->info.sqi;
	d_status = le16_to_cpu(data->info.status);
	fc = rx_mgmt_frame->frame_control;

	wil_dbg_wmi(wil, "MGMT Rx: channel %d MCS %d SNR %d SQI %d%%\n",
		    data->info.channel, data->info.mcs, data->info.snr,
		    data->info.sqi);
	wil_dbg_wmi(wil, "status 0x%04x len %d fc 0x%04x\n", d_status, d_len,
		    le16_to_cpu(fc));
	wil_dbg_wmi(wil, "qid %d mid %d cid %d\n",
		    data->info.qid, data->info.mid, data->info.cid);
	wil_hex_dump_wmi("MGMT Rx ", DUMP_PREFIX_OFFSET, 16, 1, rx_mgmt_frame,
			 d_len, true);

	if (!channel) {
		wil_err(wil, "Frame on unsupported channel\n");
		return;
	}

	if (ieee80211_is_beacon(fc) || ieee80211_is_probe_resp(fc)) {
		struct cfg80211_bss *bss;
		u64 tsf = le64_to_cpu(rx_mgmt_frame->u.beacon.timestamp);
		u16 cap = le16_to_cpu(rx_mgmt_frame->u.beacon.capab_info);
		u16 bi = le16_to_cpu(rx_mgmt_frame->u.beacon.beacon_int);
		const u8 *ie_buf = rx_mgmt_frame->u.beacon.variable;
		size_t ie_len = d_len - offsetof(struct ieee80211_mgmt,
						 u.beacon.variable);
		wil_dbg_wmi(wil, "Capability info : 0x%04x\n", cap);
		wil_dbg_wmi(wil, "TSF : 0x%016llx\n", tsf);
		wil_dbg_wmi(wil, "Beacon interval : %d\n", bi);
		wil_hex_dump_wmi("IE ", DUMP_PREFIX_OFFSET, 16, 1, ie_buf,
				 ie_len, true);

		wil_dbg_wmi(wil, "Capability info : 0x%04x\n", cap);

		bss = cfg80211_inform_bss_frame(wiphy, channel, rx_mgmt_frame,
						d_len, signal, GFP_KERNEL);
		if (bss) {
			wil_dbg_wmi(wil, "Added BSS %pM\n",
				    rx_mgmt_frame->bssid);
			cfg80211_put_bss(wiphy, bss);
		} else {
			wil_err(wil, "cfg80211_inform_bss_frame() failed\n");
		}
	} else {
		mutex_lock(&wil->p2p_wdev_mutex);
		cfg80211_rx_mgmt(wil->radio_wdev, freq, signal,
				 (void *)rx_mgmt_frame, d_len, 0);
		mutex_unlock(&wil->p2p_wdev_mutex);
	}
}

static void wmi_evt_tx_mgmt(struct wil6210_priv *wil, int id, void *d, int len)
{
	struct wmi_tx_mgmt_packet_event *data = d;
	struct ieee80211_mgmt *mgmt_frame =
			(struct ieee80211_mgmt *)data->payload;
	int flen = len - offsetof(struct wmi_tx_mgmt_packet_event, payload);

	wil_hex_dump_wmi("MGMT Tx ", DUMP_PREFIX_OFFSET, 16, 1, mgmt_frame,
			 flen, true);
}

static void wmi_evt_scan_complete(struct wil6210_priv *wil, int id,
				  void *d, int len)
{
	mutex_lock(&wil->p2p_wdev_mutex);
	if (wil->scan_request) {
		struct wmi_scan_complete_event *data = d;
		int status = le32_to_cpu(data->status);
		struct cfg80211_scan_info info = {
			.aborted = ((status != WMI_SCAN_SUCCESS) &&
				(status != WMI_SCAN_ABORT_REJECTED)),
		};

		wil_dbg_wmi(wil, "SCAN_COMPLETE(0x%08x)\n", status);
		wil_dbg_misc(wil, "Complete scan_request 0x%p aborted %d\n",
			     wil->scan_request, info.aborted);
		del_timer_sync(&wil->scan_timer);
		cfg80211_scan_done(wil->scan_request, &info);
		wil->radio_wdev = wil->wdev;
		wil->scan_request = NULL;
		wake_up_interruptible(&wil->wq);
		if (wil->p2p.pending_listen_wdev) {
			wil_dbg_misc(wil, "Scheduling delayed listen\n");
			schedule_work(&wil->p2p.delayed_listen_work);
		}
	} else {
		wil_err(wil, "SCAN_COMPLETE while not scanning\n");
	}
	mutex_unlock(&wil->p2p_wdev_mutex);
}

static void wmi_evt_connect(struct wil6210_priv *wil, int id, void *d, int len)
{
	struct net_device *ndev = wil_to_ndev(wil);
	struct wireless_dev *wdev = wil->wdev;
	struct wmi_connect_event *evt = d;
	int ch; /* channel number */
	struct station_info sinfo;
	u8 *assoc_req_ie, *assoc_resp_ie;
	size_t assoc_req_ielen, assoc_resp_ielen;
	/* capinfo(u16) + listen_interval(u16) + IEs */
	const size_t assoc_req_ie_offset = sizeof(u16) * 2;
	/* capinfo(u16) + status_code(u16) + associd(u16) + IEs */
	const size_t assoc_resp_ie_offset = sizeof(u16) * 3;
	int rc;

	if (len < sizeof(*evt)) {
		wil_err(wil, "Connect event too short : %d bytes\n", len);
		return;
	}
	if (len != sizeof(*evt) + evt->beacon_ie_len + evt->assoc_req_len +
		   evt->assoc_resp_len) {
		wil_err(wil,
			"Connect event corrupted : %d != %d + %d + %d + %d\n",
			len, (int)sizeof(*evt), evt->beacon_ie_len,
			evt->assoc_req_len, evt->assoc_resp_len);
		return;
	}
	if (evt->cid >= WIL6210_MAX_CID) {
		wil_err(wil, "Connect CID invalid : %d\n", evt->cid);
		return;
	}

	ch = evt->channel + 1;
	wil_info(wil, "Connect %pM channel [%d] cid %d aid %d\n",
		 evt->bssid, ch, evt->cid, evt->aid);
	wil_hex_dump_wmi("connect AI : ", DUMP_PREFIX_OFFSET, 16, 1,
			 evt->assoc_info, len - sizeof(*evt), true);

	/* figure out IE's */
	assoc_req_ie = &evt->assoc_info[evt->beacon_ie_len +
					assoc_req_ie_offset];
	assoc_req_ielen = evt->assoc_req_len - assoc_req_ie_offset;
	if (evt->assoc_req_len <= assoc_req_ie_offset) {
		assoc_req_ie = NULL;
		assoc_req_ielen = 0;
	}

	assoc_resp_ie = &evt->assoc_info[evt->beacon_ie_len +
					 evt->assoc_req_len +
					 assoc_resp_ie_offset];
	assoc_resp_ielen = evt->assoc_resp_len - assoc_resp_ie_offset;
	if (evt->assoc_resp_len <= assoc_resp_ie_offset) {
		assoc_resp_ie = NULL;
		assoc_resp_ielen = 0;
	}

	if (test_bit(wil_status_resetting, wil->status) ||
	    !test_bit(wil_status_fwready, wil->status)) {
		wil_err(wil, "status_resetting, cancel connect event, CID %d\n",
			evt->cid);
		/* no need for cleanup, wil_reset will do that */
		return;
	}

	mutex_lock(&wil->mutex);

	if ((wdev->iftype == NL80211_IFTYPE_STATION) ||
	    (wdev->iftype == NL80211_IFTYPE_P2P_CLIENT)) {
		if (!test_bit(wil_status_fwconnecting, wil->status)) {
			wil_err(wil, "Not in connecting state\n");
			mutex_unlock(&wil->mutex);
			return;
		}
		del_timer_sync(&wil->connect_timer);
	} else if ((wdev->iftype == NL80211_IFTYPE_AP) ||
		   (wdev->iftype == NL80211_IFTYPE_P2P_GO)) {
		if (wil->sta[evt->cid].status != wil_sta_unused) {
			wil_err(wil, "AP: Invalid status %d for CID %d\n",
				wil->sta[evt->cid].status, evt->cid);
			mutex_unlock(&wil->mutex);
			return;
		}
	}

	/* FIXME FW can transmit only ucast frames to peer */
	/* FIXME real ring_id instead of hard coded 0 */
	ether_addr_copy(wil->sta[evt->cid].addr, evt->bssid);
	wil->sta[evt->cid].status = wil_sta_conn_pending;

	rc = wil_tx_init(wil, evt->cid);
	if (rc) {
		wil_err(wil, "config tx vring failed for CID %d, rc (%d)\n",
			evt->cid, rc);
		wmi_disconnect_sta(wil, wil->sta[evt->cid].addr,
				   WLAN_REASON_UNSPECIFIED, false, false);
	} else {
		wil_info(wil, "successful connection to CID %d\n", evt->cid);
	}

	if ((wdev->iftype == NL80211_IFTYPE_STATION) ||
	    (wdev->iftype == NL80211_IFTYPE_P2P_CLIENT)) {
		if (rc) {
			netif_carrier_off(ndev);
			wil6210_bus_request(wil, WIL_DEFAULT_BUS_REQUEST_KBPS);
			wil_err(wil, "cfg80211_connect_result with failure\n");
			cfg80211_connect_result(ndev, evt->bssid, NULL, 0,
						NULL, 0,
						WLAN_STATUS_UNSPECIFIED_FAILURE,
						GFP_KERNEL);
			goto out;
		} else {
			struct wiphy *wiphy = wil_to_wiphy(wil);

			cfg80211_ref_bss(wiphy, wil->bss);
			cfg80211_connect_bss(ndev, evt->bssid, wil->bss,
					     assoc_req_ie, assoc_req_ielen,
					     assoc_resp_ie, assoc_resp_ielen,
					     WLAN_STATUS_SUCCESS, GFP_KERNEL,
					     NL80211_TIMEOUT_UNSPECIFIED);
		}
		wil->bss = NULL;
	} else if ((wdev->iftype == NL80211_IFTYPE_AP) ||
		   (wdev->iftype == NL80211_IFTYPE_P2P_GO)) {
		if (rc) {
			if (disable_ap_sme)
				/* notify new_sta has failed */
				cfg80211_del_sta(ndev, evt->bssid, GFP_KERNEL);
			goto out;
		}

		memset(&sinfo, 0, sizeof(sinfo));

		sinfo.generation = wil->sinfo_gen++;

		if (assoc_req_ie) {
			sinfo.assoc_req_ies = assoc_req_ie;
			sinfo.assoc_req_ies_len = assoc_req_ielen;
		}

		cfg80211_new_sta(ndev, evt->bssid, &sinfo, GFP_KERNEL);
	} else {
		wil_err(wil, "unhandled iftype %d for CID %d\n", wdev->iftype,
			evt->cid);
		goto out;
	}

	wil->sta[evt->cid].status = wil_sta_connected;
	wil->sta[evt->cid].aid = evt->aid;
	set_bit(wil_status_fwconnected, wil->status);
	wil_update_net_queues_bh(wil, NULL, false);

out:
	if (rc)
		wil->sta[evt->cid].status = wil_sta_unused;
	clear_bit(wil_status_fwconnecting, wil->status);
	mutex_unlock(&wil->mutex);
}

static void wmi_evt_disconnect(struct wil6210_priv *wil, int id,
			       void *d, int len)
{
	struct wmi_disconnect_event *evt = d;
	u16 reason_code = le16_to_cpu(evt->protocol_reason_status);

	wil_info(wil, "Disconnect %pM reason [proto %d wmi %d]\n",
		 evt->bssid, reason_code, evt->disconnect_reason);

	wil->sinfo_gen++;

	if (test_bit(wil_status_resetting, wil->status) ||
	    !test_bit(wil_status_fwready, wil->status)) {
		wil_err(wil, "status_resetting, cancel disconnect event\n");
		/* no need for cleanup, wil_reset will do that */
		return;
	}

	mutex_lock(&wil->mutex);
	wil6210_disconnect(wil, evt->bssid, reason_code, true);
	mutex_unlock(&wil->mutex);
}

/*
 * Firmware reports EAPOL frame using WME event.
 * Reconstruct Ethernet frame and deliver it via normal Rx
 */
static void wmi_evt_eapol_rx(struct wil6210_priv *wil, int id,
			     void *d, int len)
{
	struct net_device *ndev = wil_to_ndev(wil);
	struct wmi_eapol_rx_event *evt = d;
	u16 eapol_len = le16_to_cpu(evt->eapol_len);
	int sz = eapol_len + ETH_HLEN;
	struct sk_buff *skb;
	struct ethhdr *eth;
	int cid;
	struct wil_net_stats *stats = NULL;

	wil_dbg_wmi(wil, "EAPOL len %d from %pM\n", eapol_len,
		    evt->src_mac);

	cid = wil_find_cid(wil, evt->src_mac);
	if (cid >= 0)
		stats = &wil->sta[cid].stats;

	if (eapol_len > 196) { /* TODO: revisit size limit */
		wil_err(wil, "EAPOL too large\n");
		return;
	}

	skb = alloc_skb(sz, GFP_KERNEL);
	if (!skb) {
		wil_err(wil, "Failed to allocate skb\n");
		return;
	}

	eth = (struct ethhdr *)skb_put(skb, ETH_HLEN);
	ether_addr_copy(eth->h_dest, ndev->dev_addr);
	ether_addr_copy(eth->h_source, evt->src_mac);
	eth->h_proto = cpu_to_be16(ETH_P_PAE);
	memcpy(skb_put(skb, eapol_len), evt->eapol, eapol_len);
	skb->protocol = eth_type_trans(skb, ndev);
	if (likely(netif_rx_ni(skb) == NET_RX_SUCCESS)) {
		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += sz;
		if (stats) {
			stats->rx_packets++;
			stats->rx_bytes += sz;
		}
	} else {
		ndev->stats.rx_dropped++;
		if (stats)
			stats->rx_dropped++;
	}
}

static void wmi_evt_vring_en(struct wil6210_priv *wil, int id, void *d, int len)
{
	struct wmi_vring_en_event *evt = d;
	u8 vri = evt->vring_index;
	struct wireless_dev *wdev = wil_to_wdev(wil);

	wil_dbg_wmi(wil, "Enable vring %d\n", vri);

	if (vri >= ARRAY_SIZE(wil->vring_tx)) {
		wil_err(wil, "Enable for invalid vring %d\n", vri);
		return;
	}

	if (wdev->iftype != NL80211_IFTYPE_AP || !disable_ap_sme)
		/* in AP mode with disable_ap_sme, this is done by
		 * wil_cfg80211_change_station()
		 */
		wil->vring_tx_data[vri].dot1x_open = true;
	if (vri == wil->bcast_vring) /* no BA for bcast */
		return;
	if (agg_wsize >= 0)
		wil_addba_tx_request(wil, vri, agg_wsize);
}

static void wmi_evt_ba_status(struct wil6210_priv *wil, int id, void *d,
			      int len)
{
	struct wmi_ba_status_event *evt = d;
	struct vring_tx_data *txdata;

	wil_dbg_wmi(wil, "BACK[%d] %s {%d} timeout %d AMSDU%s\n",
		    evt->ringid,
		    evt->status == WMI_BA_AGREED ? "OK" : "N/A",
		    evt->agg_wsize, __le16_to_cpu(evt->ba_timeout),
		    evt->amsdu ? "+" : "-");

	if (evt->ringid >= WIL6210_MAX_TX_RINGS) {
		wil_err(wil, "invalid ring id %d\n", evt->ringid);
		return;
	}

	if (evt->status != WMI_BA_AGREED) {
		evt->ba_timeout = 0;
		evt->agg_wsize = 0;
		evt->amsdu = 0;
	}

	txdata = &wil->vring_tx_data[evt->ringid];

	txdata->agg_timeout = le16_to_cpu(evt->ba_timeout);
	txdata->agg_wsize = evt->agg_wsize;
	txdata->agg_amsdu = evt->amsdu;
	txdata->addba_in_progress = false;
}

static void wmi_evt_addba_rx_req(struct wil6210_priv *wil, int id, void *d,
				 int len)
{
	struct wmi_rcp_addba_req_event *evt = d;

	wil_addba_rx_request(wil, evt->cidxtid, evt->dialog_token,
			     evt->ba_param_set, evt->ba_timeout,
			     evt->ba_seq_ctrl);
}

static void wmi_evt_delba(struct wil6210_priv *wil, int id, void *d, int len)
__acquires(&sta->tid_rx_lock) __releases(&sta->tid_rx_lock)
{
	struct wmi_delba_event *evt = d;
	u8 cid, tid;
	u16 reason = __le16_to_cpu(evt->reason);
	struct wil_sta_info *sta;
	struct wil_tid_ampdu_rx *r;

	might_sleep();
	parse_cidxtid(evt->cidxtid, &cid, &tid);
	wil_dbg_wmi(wil, "DELBA CID %d TID %d from %s reason %d\n",
		    cid, tid,
		    evt->from_initiator ? "originator" : "recipient",
		    reason);
	if (!evt->from_initiator) {
		int i;
		/* find Tx vring it belongs to */
		for (i = 0; i < ARRAY_SIZE(wil->vring2cid_tid); i++) {
			if ((wil->vring2cid_tid[i][0] == cid) &&
			    (wil->vring2cid_tid[i][1] == tid)) {
				struct vring_tx_data *txdata =
					&wil->vring_tx_data[i];

				wil_dbg_wmi(wil, "DELBA Tx vring %d\n", i);
				txdata->agg_timeout = 0;
				txdata->agg_wsize = 0;
				txdata->addba_in_progress = false;

				break; /* max. 1 matching ring */
			}
		}
		if (i >= ARRAY_SIZE(wil->vring2cid_tid))
			wil_err(wil, "DELBA: unable to find Tx vring\n");
		return;
	}

	sta = &wil->sta[cid];

	spin_lock_bh(&sta->tid_rx_lock);

	r = sta->tid_rx[tid];
	sta->tid_rx[tid] = NULL;
	wil_tid_ampdu_rx_free(wil, r);

	spin_unlock_bh(&sta->tid_rx_lock);
}

/**
 * Some events are ignored for purpose; and need not be interpreted as
 * "unhandled events"
 */
static void wmi_evt_ignore(struct wil6210_priv *wil, int id, void *d, int len)
{
	wil_dbg_wmi(wil, "Ignore event 0x%04x len %d\n", id, len);
}

static const struct {
	int eventid;
	void (*handler)(struct wil6210_priv *wil, int eventid,
			void *data, int data_len);
} wmi_evt_handlers[] = {
	{WMI_READY_EVENTID,		wmi_evt_ready},
	{WMI_FW_READY_EVENTID,			wmi_evt_ignore},
	{WMI_RX_MGMT_PACKET_EVENTID,	wmi_evt_rx_mgmt},
	{WMI_TX_MGMT_PACKET_EVENTID,		wmi_evt_tx_mgmt},
	{WMI_SCAN_COMPLETE_EVENTID,	wmi_evt_scan_complete},
	{WMI_CONNECT_EVENTID,		wmi_evt_connect},
	{WMI_DISCONNECT_EVENTID,	wmi_evt_disconnect},
	{WMI_EAPOL_RX_EVENTID,		wmi_evt_eapol_rx},
	{WMI_BA_STATUS_EVENTID,		wmi_evt_ba_status},
	{WMI_RCP_ADDBA_REQ_EVENTID,	wmi_evt_addba_rx_req},
	{WMI_DELBA_EVENTID,		wmi_evt_delba},
	{WMI_VRING_EN_EVENTID,		wmi_evt_vring_en},
	{WMI_DATA_PORT_OPEN_EVENTID,		wmi_evt_ignore},
};

/*
 * Run in IRQ context
 * Extract WMI command from mailbox. Queue it to the @wil->pending_wmi_ev
 * that will be eventually handled by the @wmi_event_worker in the thread
 * context of thread "wil6210_wmi"
 */
void wmi_recv_cmd(struct wil6210_priv *wil)
{
	struct wil6210_mbox_ring_desc d_tail;
	struct wil6210_mbox_hdr hdr;
	struct wil6210_mbox_ring *r = &wil->mbox_ctl.rx;
	struct pending_wmi_event *evt;
	u8 *cmd;
	void __iomem *src;
	ulong flags;
	unsigned n;
	unsigned int num_immed_reply = 0;

	if (!test_bit(wil_status_mbox_ready, wil->status)) {
		wil_err(wil, "Reset in progress. Cannot handle WMI event\n");
		return;
	}

	for (n = 0;; n++) {
		u16 len;
		bool q;
		bool immed_reply = false;

		r->head = wil_r(wil, RGF_MBOX +
				offsetof(struct wil6210_mbox_ctl, rx.head));
		if (r->tail == r->head)
			break;

		wil_dbg_wmi(wil, "Mbox head %08x tail %08x\n",
			    r->head, r->tail);
		/* read cmd descriptor from tail */
		wil_memcpy_fromio_32(&d_tail, wil->csr + HOSTADDR(r->tail),
				     sizeof(struct wil6210_mbox_ring_desc));
		if (d_tail.sync == 0) {
			wil_err(wil, "Mbox evt not owned by FW?\n");
			break;
		}

		/* read cmd header from descriptor */
		if (0 != wmi_read_hdr(wil, d_tail.addr, &hdr)) {
			wil_err(wil, "Mbox evt at 0x%08x?\n",
				le32_to_cpu(d_tail.addr));
			break;
		}
		len = le16_to_cpu(hdr.len);
		wil_dbg_wmi(wil, "Mbox evt %04x %04x %04x %02x\n",
			    le16_to_cpu(hdr.seq), len, le16_to_cpu(hdr.type),
			    hdr.flags);

		/* read cmd buffer from descriptor */
		src = wmi_buffer(wil, d_tail.addr) +
		      sizeof(struct wil6210_mbox_hdr);
		evt = kmalloc(ALIGN(offsetof(struct pending_wmi_event,
					     event.wmi) + len, 4),
			      GFP_KERNEL);
		if (!evt)
			break;

		evt->event.hdr = hdr;
		cmd = (void *)&evt->event.wmi;
		wil_memcpy_fromio_32(cmd, src, len);
		/* mark entry as empty */
		wil_w(wil, r->tail +
		      offsetof(struct wil6210_mbox_ring_desc, sync), 0);
		/* indicate */
		if ((hdr.type == WIL_MBOX_HDR_TYPE_WMI) &&
		    (len >= sizeof(struct wmi_cmd_hdr))) {
			struct wmi_cmd_hdr *wmi = &evt->event.wmi;
			u16 id = le16_to_cpu(wmi->command_id);
			u32 tstamp = le32_to_cpu(wmi->fw_timestamp);
			spin_lock_irqsave(&wil->wmi_ev_lock, flags);
			if (wil->reply_id && wil->reply_id == id) {
				if (wil->reply_buf) {
					memcpy(wil->reply_buf, wmi,
					       min(len, wil->reply_size));
					immed_reply = true;
				}
			}
			spin_unlock_irqrestore(&wil->wmi_ev_lock, flags);

			wil_dbg_wmi(wil, "WMI event 0x%04x MID %d @%d msec\n",
				    id, wmi->mid, tstamp);
			trace_wil6210_wmi_event(wmi, &wmi[1],
						len - sizeof(*wmi));
		}
		wil_hex_dump_wmi("evt ", DUMP_PREFIX_OFFSET, 16, 1,
				 &evt->event.hdr, sizeof(hdr) + len, true);

		/* advance tail */
		r->tail = r->base + ((r->tail - r->base +
			  sizeof(struct wil6210_mbox_ring_desc)) % r->size);
		wil_w(wil, RGF_MBOX +
		      offsetof(struct wil6210_mbox_ctl, rx.tail), r->tail);

		if (immed_reply) {
			wil_dbg_wmi(wil, "recv_cmd: Complete WMI 0x%04x\n",
				    wil->reply_id);
			kfree(evt);
			num_immed_reply++;
			complete(&wil->wmi_call);
		} else {
			/* add to the pending list */
			spin_lock_irqsave(&wil->wmi_ev_lock, flags);
			list_add_tail(&evt->list, &wil->pending_wmi_ev);
			spin_unlock_irqrestore(&wil->wmi_ev_lock, flags);
			q = queue_work(wil->wmi_wq, &wil->wmi_event_worker);
			wil_dbg_wmi(wil, "queue_work -> %d\n", q);
		}
	}
	/* normally, 1 event per IRQ should be processed */
	wil_dbg_wmi(wil, "recv_cmd: -> %d events queued, %d completed\n",
		    n - num_immed_reply, num_immed_reply);
}

int wmi_call(struct wil6210_priv *wil, u16 cmdid, void *buf, u16 len,
	     u16 reply_id, void *reply, u8 reply_size, int to_msec)
{
	int rc;
	unsigned long remain;

	mutex_lock(&wil->wmi_mutex);

	spin_lock(&wil->wmi_ev_lock);
	wil->reply_id = reply_id;
	wil->reply_buf = reply;
	wil->reply_size = reply_size;
	reinit_completion(&wil->wmi_call);
	spin_unlock(&wil->wmi_ev_lock);

	rc = __wmi_send(wil, cmdid, buf, len);
	if (rc)
		goto out;

	remain = wait_for_completion_timeout(&wil->wmi_call,
					     msecs_to_jiffies(to_msec));
	if (0 == remain) {
		wil_err(wil, "wmi_call(0x%04x->0x%04x) timeout %d msec\n",
			cmdid, reply_id, to_msec);
		rc = -ETIME;
	} else {
		wil_dbg_wmi(wil,
			    "wmi_call(0x%04x->0x%04x) completed in %d msec\n",
			    cmdid, reply_id,
			    to_msec - jiffies_to_msecs(remain));
	}

out:
	spin_lock(&wil->wmi_ev_lock);
	wil->reply_id = 0;
	wil->reply_buf = NULL;
	wil->reply_size = 0;
	spin_unlock(&wil->wmi_ev_lock);

	mutex_unlock(&wil->wmi_mutex);

	return rc;
}

int wmi_echo(struct wil6210_priv *wil)
{
	struct wmi_echo_cmd cmd = {
		.value = cpu_to_le32(0x12345678),
	};

	return wmi_call(wil, WMI_ECHO_CMDID, &cmd, sizeof(cmd),
			WMI_ECHO_RSP_EVENTID, NULL, 0, 50);
}

int wmi_set_mac_address(struct wil6210_priv *wil, void *addr)
{
	struct wmi_set_mac_address_cmd cmd;

	ether_addr_copy(cmd.mac, addr);

	wil_dbg_wmi(wil, "Set MAC %pM\n", addr);

	return wmi_send(wil, WMI_SET_MAC_ADDRESS_CMDID, &cmd, sizeof(cmd));
}

int wmi_led_cfg(struct wil6210_priv *wil, bool enable)
{
	int rc = 0;
	struct wmi_led_cfg_cmd cmd = {
		.led_mode = enable,
		.id = led_id,
		.slow_blink_cfg.blink_on =
			cpu_to_le32(led_blink_time[WIL_LED_TIME_SLOW].on_ms),
		.slow_blink_cfg.blink_off =
			cpu_to_le32(led_blink_time[WIL_LED_TIME_SLOW].off_ms),
		.medium_blink_cfg.blink_on =
			cpu_to_le32(led_blink_time[WIL_LED_TIME_MED].on_ms),
		.medium_blink_cfg.blink_off =
			cpu_to_le32(led_blink_time[WIL_LED_TIME_MED].off_ms),
		.fast_blink_cfg.blink_on =
			cpu_to_le32(led_blink_time[WIL_LED_TIME_FAST].on_ms),
		.fast_blink_cfg.blink_off =
			cpu_to_le32(led_blink_time[WIL_LED_TIME_FAST].off_ms),
		.led_polarity = led_polarity,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_led_cfg_done_event evt;
	} __packed reply;

	if (led_id == WIL_LED_INVALID_ID)
		goto out;

	if (led_id > WIL_LED_MAX_ID) {
		wil_err(wil, "Invalid led id %d\n", led_id);
		rc = -EINVAL;
		goto out;
	}

	wil_dbg_wmi(wil,
		    "%s led %d\n",
		    enable ? "enabling" : "disabling", led_id);

	rc = wmi_call(wil, WMI_LED_CFG_CMDID, &cmd, sizeof(cmd),
		      WMI_LED_CFG_DONE_EVENTID, &reply, sizeof(reply),
		      100);
	if (rc)
		goto out;

	if (reply.evt.status) {
		wil_err(wil, "led %d cfg failed with status %d\n",
			led_id, le32_to_cpu(reply.evt.status));
		rc = -EINVAL;
	}

out:
	return rc;
}

int wmi_pcp_start(struct wil6210_priv *wil, int bi, u8 wmi_nettype,
		  u8 chan, u8 hidden_ssid, u8 is_go)
{
	int rc;

	struct wmi_pcp_start_cmd cmd = {
		.bcon_interval = cpu_to_le16(bi),
		.network_type = wmi_nettype,
		.disable_sec_offload = 1,
		.channel = chan - 1,
		.pcp_max_assoc_sta = max_assoc_sta,
		.hidden_ssid = hidden_ssid,
		.is_go = is_go,
		.disable_ap_sme = disable_ap_sme,
		.abft_len = wil->abft_len,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_pcp_started_event evt;
	} __packed reply;

	if (!wil->privacy)
		cmd.disable_sec = 1;

	if ((cmd.pcp_max_assoc_sta > WIL6210_MAX_CID) ||
	    (cmd.pcp_max_assoc_sta <= 0)) {
		wil_info(wil,
			 "Requested connection limit %u, valid values are 1 - %d. Setting to %d\n",
			 max_assoc_sta, WIL6210_MAX_CID, WIL6210_MAX_CID);
		cmd.pcp_max_assoc_sta = WIL6210_MAX_CID;
	}

	if (disable_ap_sme &&
	    !test_bit(WMI_FW_CAPABILITY_DISABLE_AP_SME,
		      wil->fw_capabilities)) {
		wil_err(wil, "disable_ap_sme not supported by FW\n");
		return -EOPNOTSUPP;
	}

	/*
	 * Processing time may be huge, in case of secure AP it takes about
	 * 3500ms for FW to start AP
	 */
	rc = wmi_call(wil, WMI_PCP_START_CMDID, &cmd, sizeof(cmd),
		      WMI_PCP_STARTED_EVENTID, &reply, sizeof(reply), 5000);
	if (rc)
		return rc;

	if (reply.evt.status != WMI_FW_STATUS_SUCCESS)
		rc = -EINVAL;

	if (wmi_nettype != WMI_NETTYPE_P2P)
		/* Don't fail due to error in the led configuration */
		wmi_led_cfg(wil, true);

	return rc;
}

int wmi_pcp_stop(struct wil6210_priv *wil)
{
	int rc;

	rc = wmi_led_cfg(wil, false);
	if (rc)
		return rc;

	return wmi_call(wil, WMI_PCP_STOP_CMDID, NULL, 0,
			WMI_PCP_STOPPED_EVENTID, NULL, 0, 20);
}

int wmi_set_ssid(struct wil6210_priv *wil, u8 ssid_len, const void *ssid)
{
	struct wmi_set_ssid_cmd cmd = {
		.ssid_len = cpu_to_le32(ssid_len),
	};

	if (ssid_len > sizeof(cmd.ssid))
		return -EINVAL;

	memcpy(cmd.ssid, ssid, ssid_len);

	return wmi_send(wil, WMI_SET_SSID_CMDID, &cmd, sizeof(cmd));
}

int wmi_get_ssid(struct wil6210_priv *wil, u8 *ssid_len, void *ssid)
{
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_set_ssid_cmd cmd;
	} __packed reply;
	int len; /* reply.cmd.ssid_len in CPU order */

	rc = wmi_call(wil, WMI_GET_SSID_CMDID, NULL, 0, WMI_GET_SSID_EVENTID,
		      &reply, sizeof(reply), 20);
	if (rc)
		return rc;

	len = le32_to_cpu(reply.cmd.ssid_len);
	if (len > sizeof(reply.cmd.ssid))
		return -EINVAL;

	*ssid_len = len;
	memcpy(ssid, reply.cmd.ssid, len);

	return 0;
}

int wmi_set_channel(struct wil6210_priv *wil, int channel)
{
	struct wmi_set_pcp_channel_cmd cmd = {
		.channel = channel - 1,
	};

	return wmi_send(wil, WMI_SET_PCP_CHANNEL_CMDID, &cmd, sizeof(cmd));
}

int wmi_get_channel(struct wil6210_priv *wil, int *channel)
{
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_set_pcp_channel_cmd cmd;
	} __packed reply;

	rc = wmi_call(wil, WMI_GET_PCP_CHANNEL_CMDID, NULL, 0,
		      WMI_GET_PCP_CHANNEL_EVENTID, &reply, sizeof(reply), 20);
	if (rc)
		return rc;

	if (reply.cmd.channel > 3)
		return -EINVAL;

	*channel = reply.cmd.channel + 1;

	return 0;
}

int wmi_p2p_cfg(struct wil6210_priv *wil, int channel, int bi)
{
	int rc;
	struct wmi_p2p_cfg_cmd cmd = {
		.discovery_mode = WMI_DISCOVERY_MODE_PEER2PEER,
		.bcon_interval = cpu_to_le16(bi),
		.channel = channel - 1,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_p2p_cfg_done_event evt;
	} __packed reply;

	wil_dbg_wmi(wil, "sending WMI_P2P_CFG_CMDID\n");

	rc = wmi_call(wil, WMI_P2P_CFG_CMDID, &cmd, sizeof(cmd),
		      WMI_P2P_CFG_DONE_EVENTID, &reply, sizeof(reply), 300);
	if (!rc && reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "P2P_CFG failed. status %d\n", reply.evt.status);
		rc = -EINVAL;
	}

	return rc;
}

int wmi_start_listen(struct wil6210_priv *wil)
{
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_listen_started_event evt;
	} __packed reply;

	wil_dbg_wmi(wil, "sending WMI_START_LISTEN_CMDID\n");

	rc = wmi_call(wil, WMI_START_LISTEN_CMDID, NULL, 0,
		      WMI_LISTEN_STARTED_EVENTID, &reply, sizeof(reply), 300);
	if (!rc && reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "device failed to start listen. status %d\n",
			reply.evt.status);
		rc = -EINVAL;
	}

	return rc;
}

int wmi_start_search(struct wil6210_priv *wil)
{
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_search_started_event evt;
	} __packed reply;

	wil_dbg_wmi(wil, "sending WMI_START_SEARCH_CMDID\n");

	rc = wmi_call(wil, WMI_START_SEARCH_CMDID, NULL, 0,
		      WMI_SEARCH_STARTED_EVENTID, &reply, sizeof(reply), 300);
	if (!rc && reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "device failed to start search. status %d\n",
			reply.evt.status);
		rc = -EINVAL;
	}

	return rc;
}

int wmi_stop_discovery(struct wil6210_priv *wil)
{
	int rc;

	wil_dbg_wmi(wil, "sending WMI_DISCOVERY_STOP_CMDID\n");

	rc = wmi_call(wil, WMI_DISCOVERY_STOP_CMDID, NULL, 0,
		      WMI_DISCOVERY_STOPPED_EVENTID, NULL, 0, 100);

	if (rc)
		wil_err(wil, "Failed to stop discovery\n");

	return rc;
}

int wmi_del_cipher_key(struct wil6210_priv *wil, u8 key_index,
		       const void *mac_addr, int key_usage)
{
	struct wmi_delete_cipher_key_cmd cmd = {
		.key_index = key_index,
	};

	if (mac_addr)
		memcpy(cmd.mac, mac_addr, WMI_MAC_LEN);

	return wmi_send(wil, WMI_DELETE_CIPHER_KEY_CMDID, &cmd, sizeof(cmd));
}

int wmi_add_cipher_key(struct wil6210_priv *wil, u8 key_index,
		       const void *mac_addr, int key_len, const void *key,
		       int key_usage)
{
	struct wmi_add_cipher_key_cmd cmd = {
		.key_index = key_index,
		.key_usage = key_usage,
		.key_len = key_len,
	};

	if (!key || (key_len > sizeof(cmd.key)))
		return -EINVAL;

	memcpy(cmd.key, key, key_len);
	if (mac_addr)
		memcpy(cmd.mac, mac_addr, WMI_MAC_LEN);

	return wmi_send(wil, WMI_ADD_CIPHER_KEY_CMDID, &cmd, sizeof(cmd));
}

int wmi_set_ie(struct wil6210_priv *wil, u8 type, u16 ie_len, const void *ie)
{
	static const char *const names[] = {
		[WMI_FRAME_BEACON]	= "BEACON",
		[WMI_FRAME_PROBE_REQ]	= "PROBE_REQ",
		[WMI_FRAME_PROBE_RESP]	= "WMI_FRAME_PROBE_RESP",
		[WMI_FRAME_ASSOC_REQ]	= "WMI_FRAME_ASSOC_REQ",
		[WMI_FRAME_ASSOC_RESP]	= "WMI_FRAME_ASSOC_RESP",
	};
	int rc;
	u16 len = sizeof(struct wmi_set_appie_cmd) + ie_len;
	struct wmi_set_appie_cmd *cmd = kzalloc(len, GFP_KERNEL);

	if (!cmd) {
		rc = -ENOMEM;
		goto out;
	}
	if (!ie)
		ie_len = 0;

	cmd->mgmt_frm_type = type;
	/* BUG: FW API define ieLen as u8. Will fix FW */
	cmd->ie_len = cpu_to_le16(ie_len);
	memcpy(cmd->ie_info, ie, ie_len);
	rc = wmi_send(wil, WMI_SET_APPIE_CMDID, cmd, len);
	kfree(cmd);
out:
	if (rc) {
		const char *name = type < ARRAY_SIZE(names) ?
				   names[type] : "??";
		wil_err(wil, "set_ie(%d %s) failed : %d\n", type, name, rc);
	}

	return rc;
}

/**
 * wmi_rxon - turn radio on/off
 * @on:		turn on if true, off otherwise
 *
 * Only switch radio. Channel should be set separately.
 * No timeout for rxon - radio turned on forever unless some other call
 * turns it off
 */
int wmi_rxon(struct wil6210_priv *wil, bool on)
{
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_listen_started_event evt;
	} __packed reply;

	wil_info(wil, "(%s)\n", on ? "on" : "off");

	if (on) {
		rc = wmi_call(wil, WMI_START_LISTEN_CMDID, NULL, 0,
			      WMI_LISTEN_STARTED_EVENTID,
			      &reply, sizeof(reply), 100);
		if ((rc == 0) && (reply.evt.status != WMI_FW_STATUS_SUCCESS))
			rc = -EINVAL;
	} else {
		rc = wmi_call(wil, WMI_DISCOVERY_STOP_CMDID, NULL, 0,
			      WMI_DISCOVERY_STOPPED_EVENTID, NULL, 0, 20);
	}

	return rc;
}

int wmi_rx_chain_add(struct wil6210_priv *wil, struct vring *vring)
{
	struct wireless_dev *wdev = wil->wdev;
	struct net_device *ndev = wil_to_ndev(wil);
	struct wmi_cfg_rx_chain_cmd cmd = {
		.action = WMI_RX_CHAIN_ADD,
		.rx_sw_ring = {
			.max_mpdu_size = cpu_to_le16(
				wil_mtu2macbuf(wil->rx_buf_len)),
			.ring_mem_base = cpu_to_le64(vring->pa),
			.ring_size = cpu_to_le16(vring->size),
		},
		.mid = 0, /* TODO - what is it? */
		.decap_trans_type = WMI_DECAP_TYPE_802_3,
		.reorder_type = WMI_RX_SW_REORDER,
		.host_thrsh = cpu_to_le16(rx_ring_overflow_thrsh),
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_cfg_rx_chain_done_event evt;
	} __packed evt;
	int rc;

	if (wdev->iftype == NL80211_IFTYPE_MONITOR) {
		struct ieee80211_channel *ch = wdev->preset_chandef.chan;

		cmd.sniffer_cfg.mode = cpu_to_le32(WMI_SNIFFER_ON);
		if (ch)
			cmd.sniffer_cfg.channel = ch->hw_value - 1;
		cmd.sniffer_cfg.phy_info_mode =
			cpu_to_le32(ndev->type == ARPHRD_IEEE80211_RADIOTAP);
		cmd.sniffer_cfg.phy_support =
			cpu_to_le32((wil->monitor_flags & MONITOR_FLAG_CONTROL)
				    ? WMI_SNIFFER_CP : WMI_SNIFFER_BOTH_PHYS);
	} else {
		/* Initialize offload (in non-sniffer mode).
		 * Linux IP stack always calculates IP checksum
		 * HW always calculate TCP/UDP checksum
		 */
		cmd.l3_l4_ctrl |= (1 << L3_L4_CTRL_TCPIP_CHECKSUM_EN_POS);
	}

	if (rx_align_2)
		cmd.l2_802_3_offload_ctrl |=
				L2_802_3_OFFLOAD_CTRL_SNAP_KEEP_MSK;

	/* typical time for secure PCP is 840ms */
	rc = wmi_call(wil, WMI_CFG_RX_CHAIN_CMDID, &cmd, sizeof(cmd),
		      WMI_CFG_RX_CHAIN_DONE_EVENTID, &evt, sizeof(evt), 2000);
	if (rc)
		return rc;

	vring->hwtail = le32_to_cpu(evt.evt.rx_ring_tail_ptr);

	wil_dbg_misc(wil, "Rx init: status %d tail 0x%08x\n",
		     le32_to_cpu(evt.evt.status), vring->hwtail);

	if (le32_to_cpu(evt.evt.status) != WMI_CFG_RX_CHAIN_SUCCESS)
		rc = -EINVAL;

	return rc;
}

int wmi_get_temperature(struct wil6210_priv *wil, u32 *t_bb, u32 *t_rf)
{
	int rc;
	struct wmi_temp_sense_cmd cmd = {
		.measure_baseband_en = cpu_to_le32(!!t_bb),
		.measure_rf_en = cpu_to_le32(!!t_rf),
		.measure_mode = cpu_to_le32(TEMPERATURE_MEASURE_NOW),
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_temp_sense_done_event evt;
	} __packed reply;

	rc = wmi_call(wil, WMI_TEMP_SENSE_CMDID, &cmd, sizeof(cmd),
		      WMI_TEMP_SENSE_DONE_EVENTID, &reply, sizeof(reply), 100);
	if (rc)
		return rc;

	if (t_bb)
		*t_bb = le32_to_cpu(reply.evt.baseband_t1000);
	if (t_rf)
		*t_rf = le32_to_cpu(reply.evt.rf_t1000);

	return 0;
}

int wmi_disconnect_sta(struct wil6210_priv *wil, const u8 *mac,
		       u16 reason, bool full_disconnect, bool del_sta)
{
	int rc;
	u16 reason_code;
	struct wmi_disconnect_sta_cmd disc_sta_cmd = {
		.disconnect_reason = cpu_to_le16(reason),
	};
	struct wmi_del_sta_cmd del_sta_cmd = {
		.disconnect_reason = cpu_to_le16(reason),
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_disconnect_event evt;
	} __packed reply;

	wil_dbg_wmi(wil, "disconnect_sta: (%pM, reason %d)\n", mac, reason);

	wil->locally_generated_disc = true;
	if (del_sta) {
		ether_addr_copy(del_sta_cmd.dst_mac, mac);
		rc = wmi_call(wil, WMI_DEL_STA_CMDID, &del_sta_cmd,
			      sizeof(del_sta_cmd), WMI_DISCONNECT_EVENTID,
			      &reply, sizeof(reply), 1000);
	} else {
		ether_addr_copy(disc_sta_cmd.dst_mac, mac);
		rc = wmi_call(wil, WMI_DISCONNECT_STA_CMDID, &disc_sta_cmd,
			      sizeof(disc_sta_cmd), WMI_DISCONNECT_EVENTID,
			      &reply, sizeof(reply), 1000);
	}
	/* failure to disconnect in reasonable time treated as FW error */
	if (rc) {
		wil_fw_error_recovery(wil);
		return rc;
	}

	if (full_disconnect) {
		/* call event handler manually after processing wmi_call,
		 * to avoid deadlock - disconnect event handler acquires
		 * wil->mutex while it is already held here
		 */
		reason_code = le16_to_cpu(reply.evt.protocol_reason_status);

		wil_dbg_wmi(wil, "Disconnect %pM reason [proto %d wmi %d]\n",
			    reply.evt.bssid, reason_code,
			    reply.evt.disconnect_reason);

		wil->sinfo_gen++;
		wil6210_disconnect(wil, reply.evt.bssid, reason_code, true);
	}
	return 0;
}

int wmi_addba(struct wil6210_priv *wil, u8 ringid, u8 size, u16 timeout)
{
	struct wmi_vring_ba_en_cmd cmd = {
		.ringid = ringid,
		.agg_max_wsize = size,
		.ba_timeout = cpu_to_le16(timeout),
		.amsdu = 0,
	};

	wil_dbg_wmi(wil, "addba: (ring %d size %d timeout %d)\n", ringid, size,
		    timeout);

	return wmi_send(wil, WMI_VRING_BA_EN_CMDID, &cmd, sizeof(cmd));
}

int wmi_delba_tx(struct wil6210_priv *wil, u8 ringid, u16 reason)
{
	struct wmi_vring_ba_dis_cmd cmd = {
		.ringid = ringid,
		.reason = cpu_to_le16(reason),
	};

	wil_dbg_wmi(wil, "delba_tx: (ring %d reason %d)\n", ringid, reason);

	return wmi_send(wil, WMI_VRING_BA_DIS_CMDID, &cmd, sizeof(cmd));
}

int wmi_delba_rx(struct wil6210_priv *wil, u8 cidxtid, u16 reason)
{
	struct wmi_rcp_delba_cmd cmd = {
		.cidxtid = cidxtid,
		.reason = cpu_to_le16(reason),
	};

	wil_dbg_wmi(wil, "delba_rx: (CID %d TID %d reason %d)\n", cidxtid & 0xf,
		    (cidxtid >> 4) & 0xf, reason);

	return wmi_send(wil, WMI_RCP_DELBA_CMDID, &cmd, sizeof(cmd));
}

int wmi_addba_rx_resp(struct wil6210_priv *wil, u8 cid, u8 tid, u8 token,
		      u16 status, bool amsdu, u16 agg_wsize, u16 timeout)
{
	int rc;
	struct wmi_rcp_addba_resp_cmd cmd = {
		.cidxtid = mk_cidxtid(cid, tid),
		.dialog_token = token,
		.status_code = cpu_to_le16(status),
		/* bit 0: A-MSDU supported
		 * bit 1: policy (should be 0 for us)
		 * bits 2..5: TID
		 * bits 6..15: buffer size
		 */
		.ba_param_set = cpu_to_le16((amsdu ? 1 : 0) | (tid << 2) |
					    (agg_wsize << 6)),
		.ba_timeout = cpu_to_le16(timeout),
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_rcp_addba_resp_sent_event evt;
	} __packed reply;

	wil_dbg_wmi(wil,
		    "ADDBA response for CID %d TID %d size %d timeout %d status %d AMSDU%s\n",
		    cid, tid, agg_wsize, timeout, status, amsdu ? "+" : "-");

	rc = wmi_call(wil, WMI_RCP_ADDBA_RESP_CMDID, &cmd, sizeof(cmd),
		      WMI_RCP_ADDBA_RESP_SENT_EVENTID, &reply, sizeof(reply),
		      100);
	if (rc)
		return rc;

	if (reply.evt.status) {
		wil_err(wil, "ADDBA response failed with status %d\n",
			le16_to_cpu(reply.evt.status));
		rc = -EINVAL;
	}

	return rc;
}

int wmi_ps_dev_profile_cfg(struct wil6210_priv *wil,
			   enum wmi_ps_profile_type ps_profile)
{
	int rc;
	struct wmi_ps_dev_profile_cfg_cmd cmd = {
		.ps_profile = ps_profile,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_ps_dev_profile_cfg_event evt;
	} __packed reply;
	u32 status;

	wil_dbg_wmi(wil, "Setting ps dev profile %d\n", ps_profile);

	reply.evt.status = cpu_to_le32(WMI_PS_CFG_CMD_STATUS_ERROR);

	rc = wmi_call(wil, WMI_PS_DEV_PROFILE_CFG_CMDID, &cmd, sizeof(cmd),
		      WMI_PS_DEV_PROFILE_CFG_EVENTID, &reply, sizeof(reply),
		      100);
	if (rc)
		return rc;

	status = le32_to_cpu(reply.evt.status);

	if (status != WMI_PS_CFG_CMD_STATUS_SUCCESS) {
		wil_err(wil, "ps dev profile cfg failed with status %d\n",
			status);
		rc = -EINVAL;
	}

	return rc;
}

int wmi_set_mgmt_retry(struct wil6210_priv *wil, u8 retry_short)
{
	int rc;
	struct wmi_set_mgmt_retry_limit_cmd cmd = {
		.mgmt_retry_limit = retry_short,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_set_mgmt_retry_limit_event evt;
	} __packed reply;

	wil_dbg_wmi(wil, "Setting mgmt retry short %d\n", retry_short);

	if (!test_bit(WMI_FW_CAPABILITY_MGMT_RETRY_LIMIT, wil->fw_capabilities))
		return -ENOTSUPP;

	reply.evt.status = WMI_FW_STATUS_FAILURE;

	rc = wmi_call(wil, WMI_SET_MGMT_RETRY_LIMIT_CMDID, &cmd, sizeof(cmd),
		      WMI_SET_MGMT_RETRY_LIMIT_EVENTID, &reply, sizeof(reply),
		      100);
	if (rc)
		return rc;

	if (reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "set mgmt retry limit failed with status %d\n",
			reply.evt.status);
		rc = -EINVAL;
	}

	return rc;
}

int wmi_get_mgmt_retry(struct wil6210_priv *wil, u8 *retry_short)
{
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_get_mgmt_retry_limit_event evt;
	} __packed reply;

	wil_dbg_wmi(wil, "getting mgmt retry short\n");

	if (!test_bit(WMI_FW_CAPABILITY_MGMT_RETRY_LIMIT, wil->fw_capabilities))
		return -ENOTSUPP;

	reply.evt.mgmt_retry_limit = 0;
	rc = wmi_call(wil, WMI_GET_MGMT_RETRY_LIMIT_CMDID, NULL, 0,
		      WMI_GET_MGMT_RETRY_LIMIT_EVENTID, &reply, sizeof(reply),
		      100);
	if (rc)
		return rc;

	if (retry_short)
		*retry_short = reply.evt.mgmt_retry_limit;

	return 0;
}

int wmi_abort_scan(struct wil6210_priv *wil)
{
	int rc;

	wil_dbg_wmi(wil, "sending WMI_ABORT_SCAN_CMDID\n");

	rc = wmi_send(wil, WMI_ABORT_SCAN_CMDID, NULL, 0);
	if (rc)
		wil_err(wil, "Failed to abort scan (%d)\n", rc);

	return rc;
}

int wmi_new_sta(struct wil6210_priv *wil, const u8 *mac, u8 aid)
{
	int rc;
	struct wmi_new_sta_cmd cmd = {
		.aid = aid,
	};

	wil_dbg_wmi(wil, "new sta %pM, aid %d\n", mac, aid);

	ether_addr_copy(cmd.dst_mac, mac);

	rc = wmi_send(wil, WMI_NEW_STA_CMDID, &cmd, sizeof(cmd));
	if (rc)
		wil_err(wil, "Failed to send new sta (%d)\n", rc);

	return rc;
}

void wmi_event_flush(struct wil6210_priv *wil)
{
	ulong flags;
	struct pending_wmi_event *evt, *t;

	wil_dbg_wmi(wil, "event_flush\n");

	spin_lock_irqsave(&wil->wmi_ev_lock, flags);

	list_for_each_entry_safe(evt, t, &wil->pending_wmi_ev, list) {
		list_del(&evt->list);
		kfree(evt);
	}

	spin_unlock_irqrestore(&wil->wmi_ev_lock, flags);
}

static bool wmi_evt_call_handler(struct wil6210_priv *wil, int id,
				 void *d, int len)
{
	uint i;

	for (i = 0; i < ARRAY_SIZE(wmi_evt_handlers); i++) {
		if (wmi_evt_handlers[i].eventid == id) {
			wmi_evt_handlers[i].handler(wil, id, d, len);
			return true;
		}
	}

	return false;
}

static void wmi_event_handle(struct wil6210_priv *wil,
			     struct wil6210_mbox_hdr *hdr)
{
	u16 len = le16_to_cpu(hdr->len);

	if ((hdr->type == WIL_MBOX_HDR_TYPE_WMI) &&
	    (len >= sizeof(struct wmi_cmd_hdr))) {
		struct wmi_cmd_hdr *wmi = (void *)(&hdr[1]);
		void *evt_data = (void *)(&wmi[1]);
		u16 id = le16_to_cpu(wmi->command_id);

		wil_dbg_wmi(wil, "Handle WMI 0x%04x (reply_id 0x%04x)\n",
			    id, wil->reply_id);
		/* check if someone waits for this event */
		if (wil->reply_id && wil->reply_id == id) {
			WARN_ON(wil->reply_buf);
			wmi_evt_call_handler(wil, id, evt_data,
					     len - sizeof(*wmi));
			wil_dbg_wmi(wil, "event_handle: Complete WMI 0x%04x\n",
				    id);
			complete(&wil->wmi_call);
			return;
		}
		/* unsolicited event */
		/* search for handler */
		if (!wmi_evt_call_handler(wil, id, evt_data,
					  len - sizeof(*wmi))) {
			wil_info(wil, "Unhandled event 0x%04x\n", id);
		}
	} else {
		wil_err(wil, "Unknown event type\n");
		print_hex_dump(KERN_ERR, "evt?? ", DUMP_PREFIX_OFFSET, 16, 1,
			       hdr, sizeof(*hdr) + len, true);
	}
}

/*
 * Retrieve next WMI event from the pending list
 */
static struct list_head *next_wmi_ev(struct wil6210_priv *wil)
{
	ulong flags;
	struct list_head *ret = NULL;

	spin_lock_irqsave(&wil->wmi_ev_lock, flags);

	if (!list_empty(&wil->pending_wmi_ev)) {
		ret = wil->pending_wmi_ev.next;
		list_del(ret);
	}

	spin_unlock_irqrestore(&wil->wmi_ev_lock, flags);

	return ret;
}

/*
 * Handler for the WMI events
 */
void wmi_event_worker(struct work_struct *work)
{
	struct wil6210_priv *wil = container_of(work, struct wil6210_priv,
						 wmi_event_worker);
	struct pending_wmi_event *evt;
	struct list_head *lh;

	wil_dbg_wmi(wil, "event_worker: Start\n");
	while ((lh = next_wmi_ev(wil)) != NULL) {
		evt = list_entry(lh, struct pending_wmi_event, list);
		wmi_event_handle(wil, &evt->event.hdr);
		kfree(evt);
	}
	wil_dbg_wmi(wil, "event_worker: Finished\n");
}
