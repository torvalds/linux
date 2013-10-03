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

#include <linux/etherdevice.h>
#include <linux/if_arp.h>

#include "wil6210.h"
#include "txrx.h"
#include "wmi.h"
#include "trace.h"

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
 */
static const struct {
	u32 from; /* linker address - from, inclusive */
	u32 to;   /* linker address - to, exclusive */
	u32 host; /* PCI/Host address - BAR0 + 0x880000 */
} fw_mapping[] = {
	{0x000000, 0x040000, 0x8c0000}, /* FW code RAM 256k */
	{0x800000, 0x808000, 0x900000}, /* FW data RAM 32k */
	{0x840000, 0x860000, 0x908000}, /* peripheral data RAM 128k/96k used */
	{0x880000, 0x88a000, 0x880000}, /* various RGF */
	{0x8c0000, 0x949000, 0x8c0000}, /* trivial mapping for upper area */
	/*
	 * 920000..930000 ucode code RAM
	 * 930000..932000 ucode data RAM
	 * 932000..949000 back-door debug data
	 */
};

/**
 * return AHB address for given firmware/ucode internal (linker) address
 * @x - internal address
 * If address have no valid AHB mapping, return 0
 */
static u32 wmi_addr_remap(u32 x)
{
	uint i;

	for (i = 0; i < ARRAY_SIZE(fw_mapping); i++) {
		if ((x >= fw_mapping[i].from) && (x < fw_mapping[i].to))
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
		struct wil6210_mbox_hdr_wmi wmi;
	} __packed cmd = {
		.hdr = {
			.type = WIL_MBOX_HDR_TYPE_WMI,
			.flags = 0,
			.len = cpu_to_le16(sizeof(cmd.wmi) + len),
		},
		.wmi = {
			.mid = 0,
			.id = cpu_to_le16(cmdid),
		},
	};
	struct wil6210_mbox_ring *r = &wil->mbox_ctl.tx;
	struct wil6210_mbox_ring_desc d_head;
	u32 next_head;
	void __iomem *dst;
	void __iomem *head = wmi_addr(wil, r->head);
	uint retry;

	if (sizeof(cmd) + len > r->entry_size) {
		wil_err(wil, "WMI size too large: %d bytes, max is %d\n",
			(int)(sizeof(cmd) + len), r->entry_size);
		return -ERANGE;
	}

	might_sleep();

	if (!test_bit(wil_status_fwready, &wil->status)) {
		wil_err(wil, "FW not ready\n");
		return -EAGAIN;
	}

	if (!head) {
		wil_err(wil, "WMI head is garbage: 0x%08x\n", r->head);
		return -EINVAL;
	}
	/* read Tx head till it is not busy */
	for (retry = 5; retry > 0; retry--) {
		wil_memcpy_fromio_32(&d_head, head, sizeof(d_head));
		if (d_head.sync == 0)
			break;
		msleep(20);
	}
	if (d_head.sync != 0) {
		wil_err(wil, "WMI head busy\n");
		return -EBUSY;
	}
	/* next head */
	next_head = r->base + ((r->head - r->base + sizeof(d_head)) % r->size);
	wil_dbg_wmi(wil, "Head 0x%08x -> 0x%08x\n", r->head, next_head);
	/* wait till FW finish with previous command */
	for (retry = 5; retry > 0; retry--) {
		r->tail = ioread32(wil->csr + HOST_MBOX +
				   offsetof(struct wil6210_mbox_ctl, tx.tail));
		if (next_head != r->tail)
			break;
		msleep(20);
	}
	if (next_head == r->tail) {
		wil_err(wil, "WMI ring full\n");
		return -EBUSY;
	}
	dst = wmi_buffer(wil, d_head.addr);
	if (!dst) {
		wil_err(wil, "invalid WMI buffer: 0x%08x\n",
			le32_to_cpu(d_head.addr));
		return -EINVAL;
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
	iowrite32(1, wil->csr + HOSTADDR(r->head) +
		  offsetof(struct wil6210_mbox_ring_desc, sync));
	/* advance next ptr */
	iowrite32(r->head = next_head, wil->csr + HOST_MBOX +
		  offsetof(struct wil6210_mbox_ctl, tx.head));

	trace_wil6210_wmi_cmd(&cmd.wmi, buf, len);

	/* interrupt to FW */
	iowrite32(SW_INT_MBOX, wil->csr + HOST_SW_INT);

	return 0;
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
	struct net_device *ndev = wil_to_ndev(wil);
	struct wireless_dev *wdev = wil->wdev;
	struct wmi_ready_event *evt = d;
	wil->fw_version = le32_to_cpu(evt->sw_version);
	wil->n_mids = evt->numof_additional_mids;

	wil_dbg_wmi(wil, "FW ver. %d; MAC %pM; %d MID's\n", wil->fw_version,
		    evt->mac, wil->n_mids);

	if (!is_valid_ether_addr(ndev->dev_addr)) {
		memcpy(ndev->dev_addr, evt->mac, ETH_ALEN);
		memcpy(ndev->perm_addr, evt->mac, ETH_ALEN);
	}
	snprintf(wdev->wiphy->fw_version, sizeof(wdev->wiphy->fw_version),
		 "%d", wil->fw_version);
}

static void wmi_evt_fw_ready(struct wil6210_priv *wil, int id, void *d,
			     int len)
{
	wil_dbg_wmi(wil, "WMI: FW ready\n");

	set_bit(wil_status_fwready, &wil->status);
	/* reuse wmi_ready for the firmware ready indication */
	complete(&wil->wmi_ready);
}

static void wmi_evt_rx_mgmt(struct wil6210_priv *wil, int id, void *d, int len)
{
	struct wmi_rx_mgmt_packet_event *data = d;
	struct wiphy *wiphy = wil_to_wiphy(wil);
	struct ieee80211_mgmt *rx_mgmt_frame =
			(struct ieee80211_mgmt *)data->payload;
	int ch_no = data->info.channel+1;
	u32 freq = ieee80211_channel_to_frequency(ch_no,
			IEEE80211_BAND_60GHZ);
	struct ieee80211_channel *channel = ieee80211_get_channel(wiphy, freq);
	/* TODO convert LE to CPU */
	s32 signal = 0; /* TODO */
	__le16 fc = rx_mgmt_frame->frame_control;
	u32 d_len = le32_to_cpu(data->info.len);
	u16 d_status = le16_to_cpu(data->info.status);

	wil_dbg_wmi(wil, "MGMT: channel %d MCS %d SNR %d\n",
		    data->info.channel, data->info.mcs, data->info.snr);
	wil_dbg_wmi(wil, "status 0x%04x len %d fc 0x%04x\n", d_status, d_len,
		    le16_to_cpu(fc));
	wil_dbg_wmi(wil, "qid %d mid %d cid %d\n",
		    data->info.qid, data->info.mid, data->info.cid);

	if (!channel) {
		wil_err(wil, "Frame on unsupported channel\n");
		return;
	}

	if (ieee80211_is_beacon(fc) || ieee80211_is_probe_resp(fc)) {
		struct cfg80211_bss *bss;

		bss = cfg80211_inform_bss_frame(wiphy, channel, rx_mgmt_frame,
						d_len, signal, GFP_KERNEL);
		if (bss) {
			wil_dbg_wmi(wil, "Added BSS %pM\n",
				    rx_mgmt_frame->bssid);
			cfg80211_put_bss(wiphy, bss);
		} else {
			wil_err(wil, "cfg80211_inform_bss() failed\n");
		}
	} else {
		cfg80211_rx_mgmt(wil->wdev, freq, signal,
				 (void *)rx_mgmt_frame, d_len, 0, GFP_KERNEL);
	}
}

static void wmi_evt_scan_complete(struct wil6210_priv *wil, int id,
				  void *d, int len)
{
	if (wil->scan_request) {
		struct wmi_scan_complete_event *data = d;
		bool aborted = (data->status != 0);

		wil_dbg_wmi(wil, "SCAN_COMPLETE(0x%08x)\n", data->status);
		cfg80211_scan_done(wil->scan_request, aborted);
		wil->scan_request = NULL;
	} else {
		wil_err(wil, "SCAN_COMPLETE while not scanning\n");
	}
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
	ch = evt->channel + 1;
	wil_dbg_wmi(wil, "Connect %pM channel [%d] cid %d\n",
		    evt->bssid, ch, evt->cid);
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

	if ((wdev->iftype == NL80211_IFTYPE_STATION) ||
	    (wdev->iftype == NL80211_IFTYPE_P2P_CLIENT)) {
		if (!test_bit(wil_status_fwconnecting, &wil->status)) {
			wil_err(wil, "Not in connecting state\n");
			return;
		}
		del_timer_sync(&wil->connect_timer);
		cfg80211_connect_result(ndev, evt->bssid,
					assoc_req_ie, assoc_req_ielen,
					assoc_resp_ie, assoc_resp_ielen,
					WLAN_STATUS_SUCCESS, GFP_KERNEL);

	} else if ((wdev->iftype == NL80211_IFTYPE_AP) ||
		   (wdev->iftype == NL80211_IFTYPE_P2P_GO)) {
		memset(&sinfo, 0, sizeof(sinfo));

		sinfo.generation = wil->sinfo_gen++;

		if (assoc_req_ie) {
			sinfo.assoc_req_ies = assoc_req_ie;
			sinfo.assoc_req_ies_len = assoc_req_ielen;
			sinfo.filled |= STATION_INFO_ASSOC_REQ_IES;
		}

		cfg80211_new_sta(ndev, evt->bssid, &sinfo, GFP_KERNEL);
	}
	clear_bit(wil_status_fwconnecting, &wil->status);
	set_bit(wil_status_fwconnected, &wil->status);

	/* FIXME FW can transmit only ucast frames to peer */
	/* FIXME real ring_id instead of hard coded 0 */
	memcpy(wil->dst_addr[0], evt->bssid, ETH_ALEN);

	wil->pending_connect_cid = evt->cid;
	queue_work(wil->wmi_wq_conn, &wil->connect_worker);
}

static void wmi_evt_disconnect(struct wil6210_priv *wil, int id,
			       void *d, int len)
{
	struct wmi_disconnect_event *evt = d;

	wil_dbg_wmi(wil, "Disconnect %pM reason %d proto %d wmi\n",
		    evt->bssid,
		    evt->protocol_reason_status, evt->disconnect_reason);

	wil->sinfo_gen++;

	wil6210_disconnect(wil, evt->bssid);
}

static void wmi_evt_notify(struct wil6210_priv *wil, int id, void *d, int len)
{
	struct wmi_notify_req_done_event *evt = d;

	if (len < sizeof(*evt)) {
		wil_err(wil, "Short NOTIFY event\n");
		return;
	}

	wil->stats.tsf = le64_to_cpu(evt->tsf);
	wil->stats.snr = le32_to_cpu(evt->snr_val);
	wil->stats.bf_mcs = le16_to_cpu(evt->bf_mcs);
	wil->stats.my_rx_sector = le16_to_cpu(evt->my_rx_sector);
	wil->stats.my_tx_sector = le16_to_cpu(evt->my_tx_sector);
	wil->stats.peer_rx_sector = le16_to_cpu(evt->other_rx_sector);
	wil->stats.peer_tx_sector = le16_to_cpu(evt->other_tx_sector);
	wil_dbg_wmi(wil, "Link status, MCS %d TSF 0x%016llx\n"
		    "BF status 0x%08x SNR 0x%08x\n"
		    "Tx Tpt %d goodput %d Rx goodput %d\n"
		    "Sectors(rx:tx) my %d:%d peer %d:%d\n",
		    wil->stats.bf_mcs, wil->stats.tsf, evt->status,
		    wil->stats.snr, le32_to_cpu(evt->tx_tpt),
		    le32_to_cpu(evt->tx_goodput), le32_to_cpu(evt->rx_goodput),
		    wil->stats.my_rx_sector, wil->stats.my_tx_sector,
		    wil->stats.peer_rx_sector, wil->stats.peer_tx_sector);
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

	wil_dbg_wmi(wil, "EAPOL len %d from %pM\n", eapol_len,
		    evt->src_mac);

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
	memcpy(eth->h_dest, ndev->dev_addr, ETH_ALEN);
	memcpy(eth->h_source, evt->src_mac, ETH_ALEN);
	eth->h_proto = cpu_to_be16(ETH_P_PAE);
	memcpy(skb_put(skb, eapol_len), evt->eapol, eapol_len);
	skb->protocol = eth_type_trans(skb, ndev);
	if (likely(netif_rx_ni(skb) == NET_RX_SUCCESS)) {
		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += skb->len;
	} else {
		ndev->stats.rx_dropped++;
	}
}

static void wmi_evt_linkup(struct wil6210_priv *wil, int id, void *d, int len)
{
	struct net_device *ndev = wil_to_ndev(wil);
	struct wmi_data_port_open_event *evt = d;

	wil_dbg_wmi(wil, "Link UP for CID %d\n", evt->cid);

	netif_carrier_on(ndev);
}

static void wmi_evt_linkdown(struct wil6210_priv *wil, int id, void *d, int len)
{
	struct net_device *ndev = wil_to_ndev(wil);
	struct wmi_wbe_link_down_event *evt = d;

	wil_dbg_wmi(wil, "Link DOWN for CID %d, reason %d\n",
		    evt->cid, le32_to_cpu(evt->reason));

	netif_carrier_off(ndev);
}

static void wmi_evt_ba_status(struct wil6210_priv *wil, int id, void *d,
			      int len)
{
	struct wmi_vring_ba_status_event *evt = d;

	wil_dbg_wmi(wil, "BACK[%d] %s {%d} timeout %d\n",
		    evt->ringid, evt->status ? "N/A" : "OK", evt->agg_wsize,
		    __le16_to_cpu(evt->ba_timeout));
}

static const struct {
	int eventid;
	void (*handler)(struct wil6210_priv *wil, int eventid,
			void *data, int data_len);
} wmi_evt_handlers[] = {
	{WMI_READY_EVENTID,		wmi_evt_ready},
	{WMI_FW_READY_EVENTID,		wmi_evt_fw_ready},
	{WMI_RX_MGMT_PACKET_EVENTID,	wmi_evt_rx_mgmt},
	{WMI_SCAN_COMPLETE_EVENTID,	wmi_evt_scan_complete},
	{WMI_CONNECT_EVENTID,		wmi_evt_connect},
	{WMI_DISCONNECT_EVENTID,	wmi_evt_disconnect},
	{WMI_NOTIFY_REQ_DONE_EVENTID,	wmi_evt_notify},
	{WMI_EAPOL_RX_EVENTID,		wmi_evt_eapol_rx},
	{WMI_DATA_PORT_OPEN_EVENTID,	wmi_evt_linkup},
	{WMI_WBE_LINKDOWN_EVENTID,	wmi_evt_linkdown},
	{WMI_BA_STATUS_EVENTID,		wmi_evt_ba_status},
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

	if (!test_bit(wil_status_reset_done, &wil->status)) {
		wil_err(wil, "Reset not completed\n");
		return;
	}

	for (;;) {
		u16 len;

		r->head = ioread32(wil->csr + HOST_MBOX +
				   offsetof(struct wil6210_mbox_ctl, rx.head));
		if (r->tail == r->head)
			return;

		/* read cmd from tail */
		wil_memcpy_fromio_32(&d_tail, wil->csr + HOSTADDR(r->tail),
				     sizeof(struct wil6210_mbox_ring_desc));
		if (d_tail.sync == 0) {
			wil_err(wil, "Mbox evt not owned by FW?\n");
			return;
		}

		if (0 != wmi_read_hdr(wil, d_tail.addr, &hdr)) {
			wil_err(wil, "Mbox evt at 0x%08x?\n",
				le32_to_cpu(d_tail.addr));
			return;
		}

		len = le16_to_cpu(hdr.len);
		src = wmi_buffer(wil, d_tail.addr) +
		      sizeof(struct wil6210_mbox_hdr);
		evt = kmalloc(ALIGN(offsetof(struct pending_wmi_event,
					     event.wmi) + len, 4),
			      GFP_KERNEL);
		if (!evt)
			return;

		evt->event.hdr = hdr;
		cmd = (void *)&evt->event.wmi;
		wil_memcpy_fromio_32(cmd, src, len);
		/* mark entry as empty */
		iowrite32(0, wil->csr + HOSTADDR(r->tail) +
			  offsetof(struct wil6210_mbox_ring_desc, sync));
		/* indicate */
		wil_dbg_wmi(wil, "Mbox evt %04x %04x %04x %02x\n",
			    le16_to_cpu(hdr.seq), len, le16_to_cpu(hdr.type),
			    hdr.flags);
		if ((hdr.type == WIL_MBOX_HDR_TYPE_WMI) &&
		    (len >= sizeof(struct wil6210_mbox_hdr_wmi))) {
			struct wil6210_mbox_hdr_wmi *wmi = &evt->event.wmi;
			u16 id = le16_to_cpu(wmi->id);
			u32 tstamp = le32_to_cpu(wmi->timestamp);
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
		iowrite32(r->tail, wil->csr + HOST_MBOX +
			  offsetof(struct wil6210_mbox_ctl, rx.tail));

		/* add to the pending list */
		spin_lock_irqsave(&wil->wmi_ev_lock, flags);
		list_add_tail(&evt->list, &wil->pending_wmi_ev);
		spin_unlock_irqrestore(&wil->wmi_ev_lock, flags);
		{
			int q =	queue_work(wil->wmi_wq,
					   &wil->wmi_event_worker);
			wil_dbg_wmi(wil, "queue_work -> %d\n", q);
		}
	}
}

int wmi_call(struct wil6210_priv *wil, u16 cmdid, void *buf, u16 len,
	     u16 reply_id, void *reply, u8 reply_size, int to_msec)
{
	int rc;
	int remain;

	mutex_lock(&wil->wmi_mutex);

	rc = __wmi_send(wil, cmdid, buf, len);
	if (rc)
		goto out;

	wil->reply_id = reply_id;
	wil->reply_buf = reply;
	wil->reply_size = reply_size;
	remain = wait_for_completion_timeout(&wil->wmi_ready,
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
	wil->reply_id = 0;
	wil->reply_buf = NULL;
	wil->reply_size = 0;
 out:
	mutex_unlock(&wil->wmi_mutex);

	return rc;
}

int wmi_echo(struct wil6210_priv *wil)
{
	struct wmi_echo_cmd cmd = {
		.value = cpu_to_le32(0x12345678),
	};

	return wmi_call(wil, WMI_ECHO_CMDID, &cmd, sizeof(cmd),
			 WMI_ECHO_RSP_EVENTID, NULL, 0, 20);
}

int wmi_set_mac_address(struct wil6210_priv *wil, void *addr)
{
	struct wmi_set_mac_address_cmd cmd;

	memcpy(cmd.mac, addr, ETH_ALEN);

	wil_dbg_wmi(wil, "Set MAC %pM\n", addr);

	return wmi_send(wil, WMI_SET_MAC_ADDRESS_CMDID, &cmd, sizeof(cmd));
}

int wmi_pcp_start(struct wil6210_priv *wil, int bi, u8 wmi_nettype, u8 chan)
{
	int rc;

	struct wmi_pcp_start_cmd cmd = {
		.bcon_interval = cpu_to_le16(bi),
		.network_type = wmi_nettype,
		.disable_sec_offload = 1,
		.channel = chan - 1,
	};
	struct {
		struct wil6210_mbox_hdr_wmi wmi;
		struct wmi_pcp_started_event evt;
	} __packed reply;

	if (!wil->secure_pcp)
		cmd.disable_sec = 1;

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

	return rc;
}

int wmi_pcp_stop(struct wil6210_priv *wil)
{
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
		struct wil6210_mbox_hdr_wmi wmi;
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
		struct wil6210_mbox_hdr_wmi wmi;
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

int wmi_p2p_cfg(struct wil6210_priv *wil, int channel)
{
	struct wmi_p2p_cfg_cmd cmd = {
		.discovery_mode = WMI_DISCOVERY_MODE_NON_OFFLOAD,
		.channel = channel - 1,
	};

	return wmi_send(wil, WMI_P2P_CFG_CMDID, &cmd, sizeof(cmd));
}

int wmi_del_cipher_key(struct wil6210_priv *wil, u8 key_index,
		       const void *mac_addr)
{
	struct wmi_delete_cipher_key_cmd cmd = {
		.key_index = key_index,
	};

	if (mac_addr)
		memcpy(cmd.mac, mac_addr, WMI_MAC_LEN);

	return wmi_send(wil, WMI_DELETE_CIPHER_KEY_CMDID, &cmd, sizeof(cmd));
}

int wmi_add_cipher_key(struct wil6210_priv *wil, u8 key_index,
		       const void *mac_addr, int key_len, const void *key)
{
	struct wmi_add_cipher_key_cmd cmd = {
		.key_index = key_index,
		.key_usage = WMI_KEY_USE_PAIRWISE,
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
	int rc;
	u16 len = sizeof(struct wmi_set_appie_cmd) + ie_len;
	struct wmi_set_appie_cmd *cmd = kzalloc(len, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->mgmt_frm_type = type;
	/* BUG: FW API define ieLen as u8. Will fix FW */
	cmd->ie_len = cpu_to_le16(ie_len);
	memcpy(cmd->ie_info, ie, ie_len);
	rc = wmi_send(wil, WMI_SET_APPIE_CMDID, cmd, len);
	kfree(cmd);

	return rc;
}

int wmi_rx_chain_add(struct wil6210_priv *wil, struct vring *vring)
{
	struct wireless_dev *wdev = wil->wdev;
	struct net_device *ndev = wil_to_ndev(wil);
	struct wmi_cfg_rx_chain_cmd cmd = {
		.action = WMI_RX_CHAIN_ADD,
		.rx_sw_ring = {
			.max_mpdu_size = cpu_to_le16(RX_BUF_LEN),
			.ring_mem_base = cpu_to_le64(vring->pa),
			.ring_size = cpu_to_le16(vring->size),
		},
		.mid = 0, /* TODO - what is it? */
		.decap_trans_type = WMI_DECAP_TYPE_802_3,
	};
	struct {
		struct wil6210_mbox_hdr_wmi wmi;
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
				    ? WMI_SNIFFER_CP : WMI_SNIFFER_DP);
	} else {
		/* Initialize offload (in non-sniffer mode).
		 * Linux IP stack always calculates IP checksum
		 * HW always calculate TCP/UDP checksum
		 */
		cmd.l3_l4_ctrl |= (1 << L3_L4_CTRL_TCPIP_CHECKSUM_EN_POS);
	}
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

int wmi_get_temperature(struct wil6210_priv *wil, u32 *t_m, u32 *t_r)
{
	int rc;
	struct wmi_temp_sense_cmd cmd = {
		.measure_marlon_m_en = cpu_to_le32(!!t_m),
		.measure_marlon_r_en = cpu_to_le32(!!t_r),
	};
	struct {
		struct wil6210_mbox_hdr_wmi wmi;
		struct wmi_temp_sense_done_event evt;
	} __packed reply;

	rc = wmi_call(wil, WMI_TEMP_SENSE_CMDID, &cmd, sizeof(cmd),
		      WMI_TEMP_SENSE_DONE_EVENTID, &reply, sizeof(reply), 100);
	if (rc)
		return rc;

	if (t_m)
		*t_m = le32_to_cpu(reply.evt.marlon_m_t1000);
	if (t_r)
		*t_r = le32_to_cpu(reply.evt.marlon_r_t1000);

	return 0;
}

void wmi_event_flush(struct wil6210_priv *wil)
{
	struct pending_wmi_event *evt, *t;

	wil_dbg_wmi(wil, "%s()\n", __func__);

	list_for_each_entry_safe(evt, t, &wil->pending_wmi_ev, list) {
		list_del(&evt->list);
		kfree(evt);
	}
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
	    (len >= sizeof(struct wil6210_mbox_hdr_wmi))) {
		struct wil6210_mbox_hdr_wmi *wmi = (void *)(&hdr[1]);
		void *evt_data = (void *)(&wmi[1]);
		u16 id = le16_to_cpu(wmi->id);
		/* check if someone waits for this event */
		if (wil->reply_id && wil->reply_id == id) {
			if (wil->reply_buf) {
				memcpy(wil->reply_buf, wmi,
				       min(len, wil->reply_size));
			} else {
				wmi_evt_call_handler(wil, id, evt_data,
						     len - sizeof(*wmi));
			}
			wil_dbg_wmi(wil, "Complete WMI 0x%04x\n", id);
			complete(&wil->wmi_ready);
			return;
		}
		/* unsolicited event */
		/* search for handler */
		if (!wmi_evt_call_handler(wil, id, evt_data,
					  len - sizeof(*wmi))) {
			wil_err(wil, "Unhandled event 0x%04x\n", id);
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

	while ((lh = next_wmi_ev(wil)) != NULL) {
		evt = list_entry(lh, struct pending_wmi_event, list);
		wmi_event_handle(wil, &evt->event.hdr);
		kfree(evt);
	}
}
