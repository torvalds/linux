/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DHD debugability support
 *
 * Copyright (C) 1999-2019, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dhd_debug.c 560028 2015-05-29 10:50:33Z $
 */

#include <typedefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <dhd_debug.h>

#include <event_log.h>
#include <event_trace.h>
#include <msgtrace.h>

#define DBGRING_FLUSH_THRESHOLD(ring)		(ring->ring_size / 3)
#define RING_STAT_TO_STATUS(ring, status) \
	do {               \
		strncpy(status.name, ring->name, \
			sizeof(status.name) - 1);  \
		status.ring_id = ring->id;     \
		status.ring_buffer_byte_size = ring->ring_size;  \
		status.written_bytes = ring->stat.written_bytes; \
		status.written_records = ring->stat.written_records; \
		status.read_bytes = ring->stat.read_bytes; \
		status.verbose_level = ring->log_level; \
	} while (0)

#define READ_AVAIL_SPACE(w, r, d)	((w >= r) ? (w - r) : (d - r))

struct map_table {
	uint16 fw_id;
	uint16 host_id;
	char *desc;
};

struct map_table event_map[] = {
	{WLC_E_AUTH, WIFI_EVENT_AUTH_COMPLETE, "AUTH_COMPLETE"},
	{WLC_E_ASSOC, WIFI_EVENT_ASSOC_COMPLETE, "ASSOC_COMPLETE"},
	{TRACE_FW_AUTH_STARTED, WIFI_EVENT_FW_AUTH_STARTED, "AUTH STARTED"},
	{TRACE_FW_ASSOC_STARTED, WIFI_EVENT_FW_ASSOC_STARTED, "ASSOC STARTED"},
	{TRACE_FW_RE_ASSOC_STARTED, WIFI_EVENT_FW_RE_ASSOC_STARTED, "REASSOC STARTED"},
	{TRACE_G_SCAN_STARTED, WIFI_EVENT_G_SCAN_STARTED, "GSCAN STARTED"},
	{WLC_E_PFN_SCAN_COMPLETE, WIFI_EVENT_G_SCAN_COMPLETE, "GSCAN COMPLETE"},
	{WLC_E_DISASSOC, WIFI_EVENT_DISASSOCIATION_REQUESTED, "DIASSOC REQUESTED"},
	{WLC_E_REASSOC, WIFI_EVENT_RE_ASSOCIATION_REQUESTED, "REASSOC REQUESTED"},
	{TRACE_ROAM_SCAN_STARTED, WIFI_EVENT_ROAM_REQUESTED, "ROAM REQUESTED"},
	{WLC_E_BEACON_FRAME_RX, WIFI_EVENT_BEACON_RECEIVED, "BEACON Received"},
	{TRACE_ROAM_SCAN_STARTED, WIFI_EVENT_ROAM_SCAN_STARTED, "ROAM SCAN STARTED"},
	{TRACE_ROAM_SCAN_COMPLETE, WIFI_EVENT_ROAM_SCAN_COMPLETE, "ROAM SCAN COMPLETED"},
	{TRACE_ROAM_AUTH_STARTED, WIFI_EVENT_ROAM_AUTH_STARTED, "ROAM AUTH STARTED"},
	{WLC_E_AUTH, WIFI_EVENT_ROAM_AUTH_COMPLETE, "ROAM AUTH COMPLETED"},
	{TRACE_FW_RE_ASSOC_STARTED, WIFI_EVENT_ROAM_ASSOC_STARTED, "ROAM ASSOC STARTED"},
	{WLC_E_ASSOC, WIFI_EVENT_ROAM_ASSOC_COMPLETE, "ROAM ASSOC COMPLETED"},
	{TRACE_ROAM_SCAN_COMPLETE, WIFI_EVENT_ROAM_SCAN_COMPLETE, "ROAM SCAN COMPLETED"},
	{TRACE_BT_COEX_BT_SCO_START, WIFI_EVENT_BT_COEX_BT_SCO_START, "BT SCO START"},
	{TRACE_BT_COEX_BT_SCO_STOP, WIFI_EVENT_BT_COEX_BT_SCO_STOP, "BT SCO STOP"},
	{TRACE_BT_COEX_BT_SCAN_START, WIFI_EVENT_BT_COEX_BT_SCAN_START, "BT COEX SCAN START"},
	{TRACE_BT_COEX_BT_SCAN_STOP, WIFI_EVENT_BT_COEX_BT_SCAN_STOP, "BT COEX SCAN STOP"},
	{TRACE_BT_COEX_BT_HID_START, WIFI_EVENT_BT_COEX_BT_HID_START, "BT HID START"},
	{TRACE_BT_COEX_BT_HID_STOP, WIFI_EVENT_BT_COEX_BT_HID_STOP, "BT HID STOP"},
	{WLC_E_EAPOL_MSG, WIFI_EVENT_FW_EAPOL_FRAME_RECEIVED, "FW EAPOL PKT RECEIVED"},
	{TRACE_FW_EAPOL_FRAME_TRANSMIT_START, WIFI_EVENT_FW_EAPOL_FRAME_TRANSMIT_START,
	"FW EAPOL PKT TRANSMITED"},
	{TRACE_FW_EAPOL_FRAME_TRANSMIT_STOP, WIFI_EVENT_FW_EAPOL_FRAME_TRANSMIT_STOP,
	"FW EAPOL PKT TX STOPPED"},
	{TRACE_BLOCK_ACK_NEGOTIATION_COMPLETE, WIFI_EVENT_BLOCK_ACK_NEGOTIATION_COMPLETE,
	"BLOCK ACK NEGO COMPLETED"},
};

struct map_table event_tag_map[] = {
	{TRACE_TAG_VENDOR_SPECIFIC, WIFI_TAG_VENDOR_SPECIFIC, "VENDOR SPECIFIC DATA"},
	{TRACE_TAG_BSSID, WIFI_TAG_BSSID, "BSSID"},
	{TRACE_TAG_ADDR, WIFI_TAG_ADDR, "ADDR_0"},
	{TRACE_TAG_SSID, WIFI_TAG_SSID, "SSID"},
	{TRACE_TAG_STATUS, WIFI_TAG_STATUS, "STATUS"},
	{TRACE_TAG_CHANNEL_SPEC, WIFI_TAG_CHANNEL_SPEC, "CHANSPEC"},
	{TRACE_TAG_WAKE_LOCK_EVENT, WIFI_TAG_WAKE_LOCK_EVENT, "WAKELOCK EVENT"},
	{TRACE_TAG_ADDR1, WIFI_TAG_ADDR1, "ADDR_1"},
	{TRACE_TAG_ADDR2, WIFI_TAG_ADDR2, "ADDR_2"},
	{TRACE_TAG_ADDR3, WIFI_TAG_ADDR3, "ADDR_3"},
	{TRACE_TAG_ADDR4, WIFI_TAG_ADDR4, "ADDR_4"},
	{TRACE_TAG_TSF, WIFI_TAG_TSF, "TSF"},
	{TRACE_TAG_IE, WIFI_TAG_IE, "802.11 IE"},
	{TRACE_TAG_INTERFACE, WIFI_TAG_INTERFACE, "INTERFACE"},
	{TRACE_TAG_REASON_CODE, WIFI_TAG_REASON_CODE, "REASON CODE"},
	{TRACE_TAG_RATE_MBPS, WIFI_TAG_RATE_MBPS, "RATE"},
};

/* define log level per ring type */
struct log_level_table fw_verbose_level_map[] = {
	{1, EVENT_LOG_TAG_PCI_ERROR, "PCI_ERROR"},
	{1, EVENT_LOG_TAG_PCI_WARN, "PCI_WARN"},
	{2, EVENT_LOG_TAG_PCI_INFO, "PCI_INFO"},
	{3, EVENT_LOG_TAG_PCI_DBG, "PCI_DEBUG"},
	{3, EVENT_LOG_TAG_BEACON_LOG, "BEACON_LOG"},
	{2, EVENT_LOG_TAG_WL_ASSOC_LOG, "ASSOC_LOG"},
	{2, EVENT_LOG_TAG_WL_ROAM_LOG, "ROAM_LOG"},
	{1, EVENT_LOG_TAG_TRACE_WL_INFO, "WL INFO"},
	{1, EVENT_LOG_TAG_TRACE_BTCOEX_INFO, "BTCOEX INFO"},
	{1, EVENT_LOG_TAG_SCAN_WARN, "SCAN_WARN"},
	{1, EVENT_LOG_TAG_SCAN_ERROR, "SCAN_ERROR"},
	{2, EVENT_LOG_TAG_SCAN_TRACE_LOW, "SCAN_TRACE_LOW"},
	{2, EVENT_LOG_TAG_SCAN_TRACE_HIGH, "SCAN_TRACE_HIGH"}
};

struct log_level_table fw_event_level_map[] = {
	{1, EVENT_LOG_TAG_TRACE_WL_INFO, "WL_INFO"},
	{1, EVENT_LOG_TAG_TRACE_BTCOEX_INFO, "BTCOEX_INFO"},
	{2, EVENT_LOG_TAG_BEACON_LOG, "BEACON LOG"},
};

struct map_table nan_event_map[] = {
	{TRACE_NAN_CLUSTER_STARTED, NAN_EVENT_CLUSTER_STARTED, "NAN_CLUSTER_STARTED"},
	{TRACE_NAN_CLUSTER_JOINED, NAN_EVENT_CLUSTER_JOINED, "NAN_CLUSTER_JOINED"},
	{TRACE_NAN_CLUSTER_MERGED, NAN_EVENT_CLUSTER_MERGED, "NAN_CLUSTER_MERGED"},
	{TRACE_NAN_ROLE_CHANGED, NAN_EVENT_ROLE_CHANGED, "NAN_ROLE_CHANGED"},
	{TRACE_NAN_SCAN_COMPLETE, NAN_EVENT_SCAN_COMPLETE, "NAN_SCAN_COMPLETE"},
	{TRACE_NAN_STATUS_CHNG, NAN_EVENT_STATUS_CHNG, "NAN_STATUS_CHNG"},
};

struct log_level_table nan_event_level_map[] = {
	{1, EVENT_LOG_TAG_NAN_ERROR, "NAN_ERROR"},
	{2, EVENT_LOG_TAG_NAN_INFO, "NAN_INFO"},
	{3, EVENT_LOG_TAG_NAN_DBG, "NAN_DEBUG"},
};

struct map_table nan_evt_tag_map[] = {
	{TRACE_TAG_BSSID, WIFI_TAG_BSSID, "BSSID"},
	{TRACE_TAG_ADDR, WIFI_TAG_ADDR, "ADDR_0"},
};

/* reference tab table */
uint ref_tag_tbl[EVENT_LOG_TAG_MAX + 1] = {0};

enum dbg_ring_state {
	RING_STOP	= 0,	/* ring is not initialized */
	RING_ACTIVE,	/* ring is live and logging */
	RING_SUSPEND	/* ring is initialized but not logging */
};

struct ring_statistics {
	/* number of bytes that was written to the buffer by driver */
	uint32 written_bytes;
	/* number of bytes that was read from the buffer by user land */
	uint32 read_bytes;
	/* number of records that was written to the buffer by driver */
	uint32 written_records;
};

typedef struct dhd_dbg_ring {
	int	id;		/* ring id */
	uint8	name[DBGRING_NAME_MAX];	/* name string */
	uint32	ring_size;	/* numbers of item in ring */
	uint32	wp;		/* write pointer */
	uint32	rp;		/* read pointer */
	uint32  log_level; /* log_level */
	uint32	threshold; /* threshold bytes */
	void *	ring_buf;	/* pointer of actually ring buffer */
	void *	lock;		/* spin lock for ring access */
	struct ring_statistics stat; /* statistics */
	enum dbg_ring_state state;	/* ring state enum */
} dhd_dbg_ring_t;

typedef struct dhd_dbg {
	dhd_dbg_ring_t dbg_rings[DEBUG_RING_ID_MAX];
	void *private;		/* os private_data */
	dbg_pullreq_t pullreq;
	dbg_urgent_noti_t urgent_notifier;
} dhd_dbg_t;

typedef struct dhddbg_loglist_item {
	dll_t list;
	event_log_hdr_t *hdr;
} loglist_item_t;

typedef struct dhbdbg_pending_item {
	dll_t list;
	dhd_dbg_ring_status_t ring_status;
	dhd_dbg_ring_entry_t *ring_entry;
} pending_item_t;

/* get next entry; offset must point to valid entry */
static uint32
next_entry(dhd_dbg_ring_t *ring, int32 offset)
{
	dhd_dbg_ring_entry_t *entry =
		(dhd_dbg_ring_entry_t *)((uint8 *)ring->ring_buf + offset);

	/*
	 * A length == 0 record is the end of buffer marker. Wrap around and
	 * read the message at the start of the buffer as *this* one, and
	 * return the one after that.
	 */
	if (!entry->len) {
		entry = (dhd_dbg_ring_entry_t *)ring->ring_buf;
		return ENTRY_LENGTH(entry);
	}
	return offset + ENTRY_LENGTH(entry);
}

/* get record by offset; idx must point to valid entry */
static dhd_dbg_ring_entry_t *
get_entry(dhd_dbg_ring_t *ring, int32 offset)
{
	dhd_dbg_ring_entry_t *entry =
		(dhd_dbg_ring_entry_t *)((uint8 *)ring->ring_buf + offset);

	/*
	 * A length == 0 record is the end of buffer marker. Wrap around and
	 * read the message at the start of the buffer.
	 */
	if (!entry->len)
		return (dhd_dbg_ring_entry_t *)ring->ring_buf;
	return entry;
}

int
dhd_dbg_ring_pull(dhd_pub_t *dhdp, int ring_id, void *data, uint32 buf_len)
{
	uint32 avail_len, r_len = 0;
	unsigned long flags;
	dhd_dbg_ring_t *ring;
	dhd_dbg_ring_entry_t *hdr;

	if (!dhdp || !dhdp->dbg)
		return r_len;
	ring = &dhdp->dbg->dbg_rings[ring_id];
	if (ring->state != RING_ACTIVE)
		return r_len;

	flags = dhd_os_spin_lock(ring->lock);

	/* get a fresh pending length */
	avail_len = READ_AVAIL_SPACE(ring->wp, ring->rp, ring->ring_size);
	while (avail_len > 0 && buf_len > 0) {
		hdr = get_entry(ring, ring->rp);
		memcpy(data, hdr, ENTRY_LENGTH(hdr));
		r_len += ENTRY_LENGTH(hdr);
		/* update read pointer */
		ring->rp = next_entry(ring, ring->rp);
		data = (uint8 *)data + ENTRY_LENGTH(hdr);
		avail_len -= ENTRY_LENGTH(hdr);
		buf_len -= ENTRY_LENGTH(hdr);
		ring->stat.read_bytes += ENTRY_LENGTH(hdr);
		DHD_DBGIF(("%s read_bytes  %d\n", __FUNCTION__,
			ring->stat.read_bytes));
	}
	dhd_os_spin_unlock(ring->lock, flags);

	return r_len;
}

int
dhd_dbg_ring_push(dhd_pub_t *dhdp, int ring_id, dhd_dbg_ring_entry_t *hdr, void *data)
{
	unsigned long flags;
	uint32 w_len;
	dhd_dbg_ring_t *ring;
	dhd_dbg_ring_entry_t *w_entry;
	if (!dhdp || !dhdp->dbg)
		return BCME_BADADDR;

	ring = &dhdp->dbg->dbg_rings[ring_id];

	if (ring->state != RING_ACTIVE)
		return BCME_OK;

	flags = dhd_os_spin_lock(ring->lock);

	w_len = ENTRY_LENGTH(hdr);
	/* prep the space */
	do {
		if (ring->rp == ring->wp)
			break;
		if (ring->rp < ring->wp) {
			if (ring->ring_size - ring->wp == w_len) {
				if (ring->rp == 0)
					ring->rp = next_entry(ring, ring->rp);
				break;
			} else if (ring->ring_size - ring->wp < w_len) {
				if (ring->rp == 0)
					ring->rp = next_entry(ring, ring->rp);
				/* 0 pad insufficient tail space */
				memset((uint8 *)ring->ring_buf + ring->wp, 0,
				       DBG_RING_ENTRY_SIZE);
				ring->wp = 0;
				continue;
			} else {
				break;
			}
		}
		if (ring->rp > ring->wp) {
			if (ring->rp - ring->wp <= w_len) {
				ring->rp = next_entry(ring, ring->rp);
				continue;
			} else {
				break;
			}
		}
	} while (1);

	w_entry = (dhd_dbg_ring_entry_t *)((uint8 *)ring->ring_buf + ring->wp);
	/* header */
	memcpy(w_entry, hdr, DBG_RING_ENTRY_SIZE);
	w_entry->len = hdr->len;
	/* payload */
	memcpy((char *)w_entry + DBG_RING_ENTRY_SIZE, data, w_entry->len);
	/* update write pointer */
	ring->wp += w_len;
	/* update statistics */
	ring->stat.written_records++;
	ring->stat.written_bytes += w_len;
	dhd_os_spin_unlock(ring->lock, flags);
	DHD_DBGIF(("%s : written_records %d, written_bytes %d\n", __FUNCTION__,
		ring->stat.written_records, ring->stat.written_bytes));

	/* if the current pending size is bigger than threshold */
	if (ring->threshold > 0 &&
		(READ_AVAIL_SPACE(ring->wp, ring->rp, ring->ring_size) >=
	    ring->threshold))
		dhdp->dbg->pullreq(dhdp->dbg->private, ring->id);
	return  BCME_OK;
}

static int
dhd_dbg_msgtrace_seqchk(uint32 *prev, uint32 cur)
{
	/* normal case including wrap around */
	if ((cur == 0 && *prev == 0xFFFFFFFF) || ((cur - *prev) == 1)) {
		goto done;
	} else if (cur == *prev) {
		DHD_EVENT(("%s duplicate trace\n", __FUNCTION__));
		return -1;
	} else if (cur > *prev) {
		DHD_EVENT(("%s lost %d packets\n", __FUNCTION__, cur - *prev));
	} else {
		DHD_EVENT(("%s seq out of order, dhd %d, dongle %d\n",
			__FUNCTION__, *prev, cur));
	}
done:
	*prev = cur;
	return 0;
}

static void
dhd_dbg_msgtrace_msg_parser(void *event_data)
{
	msgtrace_hdr_t *hdr;
	char *data, *s;
	static uint32 seqnum_prev = 0;

	hdr = (msgtrace_hdr_t *)event_data;
	data = (char *)event_data + MSGTRACE_HDRLEN;

	/* There are 2 bytes available at the end of data */
	data[ntoh16(hdr->len)] = '\0';

	if (ntoh32(hdr->discarded_bytes) || ntoh32(hdr->discarded_printf)) {
		DHD_DBGIF(("WLC_E_TRACE: [Discarded traces in dongle -->"
			"discarded_bytes %d discarded_printf %d]\n",
			ntoh32(hdr->discarded_bytes),
			ntoh32(hdr->discarded_printf)));
	}

	if (dhd_dbg_msgtrace_seqchk(&seqnum_prev, ntoh32(hdr->seqnum)))
		return;

	/* Display the trace buffer. Advance from
	 * \n to \n to avoid display big
	 * printf (issue with Linux printk )
	 */
	while (*data != '\0' && (s = strstr(data, "\n")) != NULL) {
		*s = '\0';
		DHD_FWLOG(("[FWLOG] %s\n", data));
		data = s+1;
	}
	if (*data)
		DHD_FWLOG(("[FWLOG] %s", data));
}

#ifdef SHOW_LOGTRACE
static const uint8 *
event_get_tlv(uint16 id, const char* tlvs, uint tlvs_len)
{
	const uint8 *pos = tlvs;
	const uint8 *end = pos + tlvs_len;
	tlv_log *tlv;
	int rest;

	while (pos + 1 < end) {
		if (pos + 4 + pos[1] > end)
			break;
		tlv = (tlv_log *) pos;
		if (tlv->tag == id)
			return pos;
		rest = tlv->len % 4; /* padding values */
		pos += 4 + tlv->len + rest;
	}
	return NULL;
}

#define DATA_UNIT_FOR_LOG_CNT 4
static int
dhd_dbg_nan_event_handler(dhd_pub_t *dhdp, event_log_hdr_t *hdr, uint32 *data)
{
	int ret = BCME_OK;
	wl_event_log_id_t nan_hdr;
	log_nan_event_t *evt_payload;
	uint16 evt_payload_len = 0, tot_payload_len = 0;
	dhd_dbg_ring_entry_t msg_hdr;
	bool evt_match = FALSE;
	event_log_hdr_t *ts_hdr;
	uint32 *ts_data;
	char *tlvs, *dest_tlvs;
	tlv_log *tlv_data;
	int tlv_len = 0;
	int i = 0, evt_idx = 0;
	char eaddr_buf[ETHER_ADDR_STR_LEN];

	BCM_REFERENCE(eaddr_buf);

	nan_hdr.t = *data;
	DHD_DBGIF(("%s: version %u event %x\n", __FUNCTION__, nan_hdr.version,
		nan_hdr.event));

	if (nan_hdr.version != DIAG_VERSION) {
		DHD_ERROR(("Event payload version %u mismatch with current version %u\n",
			nan_hdr.version, DIAG_VERSION));
		return BCME_VERSION;
	}
	memset(&msg_hdr, 0, sizeof(dhd_dbg_ring_entry_t));
	ts_hdr = (event_log_hdr_t *)((uint8 *)data - sizeof(event_log_hdr_t));
	if (ts_hdr->tag == EVENT_LOG_TAG_TS) {
		ts_data = (uint32 *)ts_hdr - ts_hdr->count;
		msg_hdr.timestamp = (uint64)ts_data[0];
		msg_hdr.flags |= DBG_RING_ENTRY_FLAGS_HAS_TIMESTAMP;
	}
	msg_hdr.type = DBG_RING_ENTRY_NAN_EVENT_TYPE;
	for (i = 0; i < ARRAYSIZE(nan_event_map); i++) {
		if (nan_event_map[i].fw_id == nan_hdr.event) {
			evt_match = TRUE;
			evt_idx = i;
			break;
		}
	}
	if (evt_match) {
		DHD_DBGIF(("%s : event (%s)\n", __FUNCTION__, nan_event_map[evt_idx].desc));
		/* payload length for nan event data */
		evt_payload_len = sizeof(log_nan_event_t) +
			(hdr->count - 2) * DATA_UNIT_FOR_LOG_CNT;
		if ((evt_payload = MALLOC(dhdp->osh, evt_payload_len)) == NULL) {
			DHD_ERROR(("Memory allocation failed for nan evt log (%u)\n",
				evt_payload_len));
			return BCME_NOMEM;
		}
		evt_payload->version = NAN_EVENT_VERSION;
		evt_payload->event = nan_event_map[evt_idx].host_id;
		dest_tlvs = (char *)evt_payload->tlvs;
		tot_payload_len = sizeof(log_nan_event_t);
		tlvs = (char *)(&data[1]);
		tlv_len = (hdr->count - 2) * DATA_UNIT_FOR_LOG_CNT;
		for (i = 0; i < ARRAYSIZE(nan_evt_tag_map); i++) {
			tlv_data = (tlv_log *)event_get_tlv(nan_evt_tag_map[i].fw_id,
				tlvs, tlv_len);
			if (tlv_data) {
				DHD_DBGIF(("NAN evt tlv.tag(%s), tlv.len : %d, tlv.data :  ",
					nan_evt_tag_map[i].desc, tlv_data->len));
				memcpy(dest_tlvs, tlv_data, sizeof(tlv_log) + tlv_data->len);
				tot_payload_len += tlv_data->len + sizeof(tlv_log);
				switch (tlv_data->tag) {
					case TRACE_TAG_BSSID:
					case TRACE_TAG_ADDR:
						DHD_DBGIF(("%s\n",
						bcm_ether_ntoa(
							(const struct ether_addr *)tlv_data->value,
							eaddr_buf)));
					break;
					default:
						if (DHD_DBGIF_ON()) {
							prhex(NULL, &tlv_data->value[0],
								tlv_data->len);
						}
					break;
				}
				dest_tlvs += tlv_data->len + sizeof(tlv_log);
			}
		}
		msg_hdr.flags |= DBG_RING_ENTRY_FLAGS_HAS_BINARY;
		msg_hdr.len = tot_payload_len;
		dhd_dbg_ring_push(dhdp, NAN_EVENT_RING_ID, &msg_hdr, evt_payload);
		MFREE(dhdp->osh, evt_payload, evt_payload_len);
	}
	return ret;
}

static int
dhd_dbg_custom_evnt_handler(dhd_pub_t *dhdp, event_log_hdr_t *hdr, uint32 *data)
{
	int i = 0, match_idx = 0;
	int payload_len, tlv_len;
	uint16 tot_payload_len = 0;
	int ret = BCME_OK;
	int log_level;
	wl_event_log_id_t wl_log_id;
	dhd_dbg_ring_entry_t msg_hdr;
	log_conn_event_t *event_data;
	bool evt_match = FALSE;
	event_log_hdr_t *ts_hdr;
	uint32 *ts_data;
	char *tlvs, *dest_tlvs;
	tlv_log *tlv_data;
	static uint64 ts_saved = 0;
	char eabuf[ETHER_ADDR_STR_LEN];
	char chanbuf[CHANSPEC_STR_LEN];

	BCM_REFERENCE(eabuf);
	BCM_REFERENCE(chanbuf);
	/* get a event type and version */
	wl_log_id.t = *data;
	if (wl_log_id.version != DIAG_VERSION)
		return BCME_VERSION;

	ts_hdr = (event_log_hdr_t *)((uint8 *)data - sizeof(event_log_hdr_t));
	if (ts_hdr->tag == EVENT_LOG_TAG_TS) {
		ts_data = (uint32 *)ts_hdr - ts_hdr->count;
		ts_saved = (uint64)ts_data[0];
	}
	memset(&msg_hdr, 0, sizeof(dhd_dbg_ring_entry_t));
	msg_hdr.timestamp = ts_saved;

	DHD_DBGIF(("Android Event ver %d, payload %d words, ts %llu\n",
		(*data >> 16), hdr->count - 1, ts_saved));

	/* Perform endian convertion */
	for (i = 0; i < hdr->count; i++) {
		/* *(data + i) = ntoh32(*(data + i)); */
		DHD_DATA(("%08x ", *(data + i)));
	}
	DHD_DATA(("\n"));
	msg_hdr.flags |= DBG_RING_ENTRY_FLAGS_HAS_TIMESTAMP;
	msg_hdr.flags |= DBG_RING_ENTRY_FLAGS_HAS_BINARY;
	msg_hdr.type = DBG_RING_ENTRY_EVENT_TYPE;

	/* convert the data to log_conn_event_t format */
	for (i = 0; i < ARRAYSIZE(event_map); i++) {
		if (event_map[i].fw_id == wl_log_id.event) {
			evt_match = TRUE;
			match_idx = i;
			break;
		}
	}
	if (evt_match) {
		log_level = dhdp->dbg->dbg_rings[FW_EVENT_RING_ID].log_level;
		/* filter the data based on log_level */
		for (i = 0; i < ARRAYSIZE(fw_event_level_map); i++) {
			if ((fw_event_level_map[i].tag == hdr->tag) &&
				(fw_event_level_map[i].log_level > log_level)) {
				return BCME_OK;
			}
		}
		DHD_DBGIF(("%s : event (%s)\n", __FUNCTION__, event_map[match_idx].desc));
		/* get the payload length for event data (skip : log header + timestamp) */
		payload_len = sizeof(log_conn_event_t) + DATA_UNIT_FOR_LOG_CNT * (hdr->count - 2);
		event_data = MALLOC(dhdp->osh, payload_len);
		if (!event_data) {
			DHD_ERROR(("failed to allocate the log_conn_event_t with length(%d)\n",
				payload_len));
			return BCME_NOMEM;
		}
		event_data->event = event_map[match_idx].host_id;
		dest_tlvs = (char *)event_data->tlvs;
		tot_payload_len = sizeof(log_conn_event_t);
		tlvs = (char *)(&data[1]);
		tlv_len = (hdr->count - 2) * DATA_UNIT_FOR_LOG_CNT;
		for (i = 0; i < ARRAYSIZE(event_tag_map); i++) {
			tlv_data = (tlv_log *)event_get_tlv(event_tag_map[i].fw_id, tlvs, tlv_len);
			if (tlv_data) {
				DHD_DBGIF(("tlv.tag(%s), tlv.len : %d, tlv.data :  ",
					event_tag_map[i].desc, tlv_data->len));
				memcpy(dest_tlvs, tlv_data, sizeof(tlv_log) + tlv_data->len);
				tot_payload_len += tlv_data->len + sizeof(tlv_log);
				switch (tlv_data->tag) {
				case TRACE_TAG_BSSID:
				case TRACE_TAG_ADDR:
				case TRACE_TAG_ADDR1:
				case TRACE_TAG_ADDR2:
				case TRACE_TAG_ADDR3:
				case TRACE_TAG_ADDR4:
					DHD_DBGIF(("%s\n",
					bcm_ether_ntoa((const struct ether_addr *)tlv_data->value,
							eabuf)));
					break;
				case TRACE_TAG_SSID:
					DHD_DBGIF(("%s\n", tlv_data->value));
					break;
				case TRACE_TAG_STATUS:
					DHD_DBGIF(("%d\n", ltoh32_ua(&tlv_data->value[0])));
					break;
				case TRACE_TAG_REASON_CODE:
					DHD_DBGIF(("%d\n", ltoh16_ua(&tlv_data->value[0])));
					break;
				case TRACE_TAG_RATE_MBPS:
					DHD_DBGIF(("%d Kbps\n",
						ltoh16_ua(&tlv_data->value[0]) * 500));
					break;
				case TRACE_TAG_CHANNEL_SPEC:
					DHD_DBGIF(("%s\n",
						wf_chspec_ntoa(
							ltoh16_ua(&tlv_data->value[0]), chanbuf)));
					break;
				default:
					if (DHD_DBGIF_ON()) {
						prhex(NULL, &tlv_data->value[0], tlv_data->len);
					}
				}
				dest_tlvs += tlv_data->len + sizeof(tlv_log);
			}
		}
		msg_hdr.len = tot_payload_len;
		dhd_dbg_ring_push(dhdp, FW_EVENT_RING_ID, &msg_hdr, event_data);
		MFREE(dhdp->osh, event_data, payload_len);
	}
	return ret;
}

#define MAX_NO_OF_ARG	16
#define FMTSTR_SIZE	132
#define ROMSTR_SIZE	200
#define SIZE_LOC_STR	50
static void
dhd_dbg_verboselog_handler(dhd_pub_t *dhdp, event_log_hdr_t *hdr,
		void *raw_event_ptr)
{
	dhd_event_log_t *raw_event = (dhd_event_log_t *)raw_event_ptr;
	event_log_hdr_t *ts_hdr;
	uint32 *log_ptr = (uint32 *)hdr - hdr->count;
	uint16 count;
	int log_level, id;
	char fmtstr_loc_buf[ROMSTR_SIZE] = { 0 };
	uint32 rom_str_len = 0;
	char (*str_buf)[SIZE_LOC_STR] = NULL;
	char *str_tmpptr = NULL;
	uint32 addr = 0;
	typedef union {
		uint32 val;
		char * addr;
	} u_arg;
	u_arg arg[MAX_NO_OF_ARG] = {{0}};
	char *c_ptr = NULL;
	uint32 *ts_data;
	static uint64 ts_saved = 0;
	dhd_dbg_ring_entry_t msg_hdr;
	struct bcmstrbuf b;
	UNUSED_PARAMETER(arg);

	/* Get time stamp if it's updated */
	ts_hdr = (void *)log_ptr - sizeof(event_log_hdr_t);
	if (ts_hdr->tag == EVENT_LOG_TAG_TS) {
		ts_data = (uint32 *)ts_hdr - ts_hdr->count;
		ts_saved = (uint64)ts_data[0];
		DHD_MSGTRACE_LOG(("EVENT_LOG_TS[0x%08x]: SYS:%08x CPU:%08x\n",
			ts_data[ts_hdr->count - 1], ts_data[0], ts_data[1]));
	}

	if (hdr->tag == EVENT_LOG_TAG_ROM_PRINTF) {
		rom_str_len = (hdr->count - 1) * sizeof(uint32);
		if (rom_str_len >= (ROMSTR_SIZE -1))
			rom_str_len = ROMSTR_SIZE - 1;

		/* copy all ascii data for ROM printf to local string */
		memcpy(fmtstr_loc_buf, log_ptr, rom_str_len);
		/* add end of line at last */
		fmtstr_loc_buf[rom_str_len] = '\0';

		DHD_MSGTRACE_LOG(("EVENT_LOG_ROM[0x%08x]: %s",
				log_ptr[hdr->count - 1], fmtstr_loc_buf));

		/* Add newline if missing */
		if (fmtstr_loc_buf[strlen(fmtstr_loc_buf) - 1] != '\n')
			DHD_MSGTRACE_LOG(("\n"));

		return;
	}

	/* print the message out in a logprint  */
	if (!(((raw_event->raw_sstr) || (raw_event->rom_raw_sstr)) &&
		raw_event->fmts) || hdr->fmt_num == 0xffff) {
		if (dhdp->dbg) {
			log_level = dhdp->dbg->dbg_rings[FW_VERBOSE_RING_ID].log_level;
			for (id = 0; id < ARRAYSIZE(fw_verbose_level_map); id++) {
				if ((fw_verbose_level_map[id].tag == hdr->tag) &&
					(fw_verbose_level_map[id].log_level > log_level))
					return;
			}
		}

		bcm_binit(&b, fmtstr_loc_buf, FMTSTR_SIZE);
		bcm_bprintf(&b, "%d.%d EL: %x %x", (uint32)ts_saved / 1000,
			(uint32)ts_saved % 1000,
			hdr->tag & EVENT_LOG_TAG_FLAG_SET_MASK,
			hdr->fmt_num);
		for (count = 0; count < (hdr->count - 1); count++)
			bcm_bprintf(&b, " %x", log_ptr[count]);
		bcm_bprintf(&b, "\n");
		DHD_MSGTRACE_LOG(("%s\n", b.origbuf));
		if (!dhdp->dbg)
			return;
		memset(&msg_hdr, 0, sizeof(dhd_dbg_ring_entry_t));
		msg_hdr.timestamp = ts_saved;
		msg_hdr.flags |= DBG_RING_ENTRY_FLAGS_HAS_TIMESTAMP;
		msg_hdr.type = DBG_RING_ENTRY_DATA_TYPE;
		msg_hdr.len = b.origsize - b.size;
		dhd_dbg_ring_push(dhdp, FW_VERBOSE_RING_ID, &msg_hdr,
				b.origbuf);
		return;
	}

	str_buf = MALLOCZ(dhdp->osh, (MAX_NO_OF_ARG * SIZE_LOC_STR));
	if (!str_buf) {
		DHD_ERROR(("%s: malloc failed str_buf\n", __FUNCTION__));
		return;
	}

	if ((hdr->fmt_num >> 2) < raw_event->num_fmts) {
		snprintf(fmtstr_loc_buf, FMTSTR_SIZE, "CONSOLE_E: [0x%x] %s",
			log_ptr[hdr->count-1],
			raw_event->fmts[hdr->fmt_num >> 2]);
		c_ptr = fmtstr_loc_buf;
	} else {
		DHD_ERROR(("%s: fmt number out of range \n", __FUNCTION__));
		goto exit;
	}

	for (count = 0; count < (hdr->count - 1); count++) {
		if (c_ptr != NULL)
			if ((c_ptr = strstr(c_ptr, "%")) != NULL)
				c_ptr++;

		if ((c_ptr != NULL) && (*c_ptr == 's')) {
			if ((raw_event->raw_sstr) &&
				((log_ptr[count] > raw_event->rodata_start) &&
				(log_ptr[count] < raw_event->rodata_end))) {
				/* ram static string */
				addr = log_ptr[count] - raw_event->rodata_start;
				str_tmpptr = raw_event->raw_sstr + addr;
				memcpy(str_buf[count], str_tmpptr,
					SIZE_LOC_STR);
				str_buf[count][SIZE_LOC_STR-1] = '\0';
				arg[count].addr = str_buf[count];
			} else if ((raw_event->rom_raw_sstr) &&
					((log_ptr[count] >
					raw_event->rom_rodata_start) &&
					(log_ptr[count] <
					raw_event->rom_rodata_end))) {
				/* rom static string */
				addr = log_ptr[count] - raw_event->rom_rodata_start;
				str_tmpptr = raw_event->rom_raw_sstr + addr;
				memcpy(str_buf[count], str_tmpptr,
					SIZE_LOC_STR);
				str_buf[count][SIZE_LOC_STR-1] = '\0';
				arg[count].addr = str_buf[count];
			} else {
				/*
				*  Dynamic string OR
				* No data for static string.
				* So store all string's address as string.
				*/
				snprintf(str_buf[count], SIZE_LOC_STR,
					"(s)0x%x", log_ptr[count]);
				arg[count].addr = str_buf[count];
			}
		} else {
			/* Other than string */
			arg[count].val = log_ptr[count];
		}
		DHD_DBGIF(("Arg val = %d and address = %p\n", arg[count].val, arg[count].addr));
	}
	DHD_EVENT((fmtstr_loc_buf, arg[0], arg[1], arg[2], arg[3],
		arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10],
		arg[11], arg[12], arg[13], arg[14], arg[15]));

exit:
	MFREE(dhdp->osh, str_buf, (MAX_NO_OF_ARG * SIZE_LOC_STR));
}

static void
dhd_dbg_msgtrace_log_parser(dhd_pub_t *dhdp, void *event_data,
	void *raw_event_ptr, uint datalen)
{
	msgtrace_hdr_t *hdr;
	char *data;
	int id;
	uint32 hdrlen = sizeof(event_log_hdr_t);
	static uint32 seqnum_prev = 0;
	event_log_hdr_t *log_hdr;
	bool msg_processed = FALSE;
	uint32 *log_ptr =  NULL;
	dll_t list_head, *cur;
	loglist_item_t *log_item;
	uint32 nan_evt_ring_log_level = 0;

	hdr = (msgtrace_hdr_t *)event_data;
	data = (char *)event_data + MSGTRACE_HDRLEN;
	datalen -= MSGTRACE_HDRLEN;

	if (dhd_dbg_msgtrace_seqchk(&seqnum_prev, ntoh32(hdr->seqnum)))
		return;

	DHD_MSGTRACE_LOG(("EVENT_LOG_HDR[No.%d]: timestamp 0x%08x length = %d\n",
		ltoh16(*((uint16 *)data)), ltoh16(*((uint16 *)(data + 2))),
		ltoh32(*((uint32 *)(data+4)))));
	data += 8;
	datalen -= 8;

	/* start parsing from the tail of packet
	 * Sameple format of a meessage
	 * 001d3c54 00000064 00000064 001d3c54 001dba08 035d6ce1 0c540639
	 * 001d3c54 00000064 00000064 035d6d89 0c580439
	 * 0x0c580439 -- 39 is tag, 04 is count, 580c is format number
	 * all these uint32 values comes in reverse order as group as EL data
	 * while decoding we can only parse from last to first
	 */
	dll_init(&list_head);
	while (datalen > 0) {
		log_hdr = (event_log_hdr_t *)(data + datalen - hdrlen);
		/* pratially overwritten entries */
		if ((uint32 *)log_hdr - (uint32 *)data < log_hdr->count)
			break;
		/* Check argument count (only when format is valid) */
		if ((log_hdr->count > MAX_NO_OF_ARG) &&
		    (log_hdr->fmt_num != 0xffff))
			break;
		/* end of frame? */
		if (log_hdr->tag == EVENT_LOG_TAG_NULL) {
			log_hdr--;
			datalen -= hdrlen;
			continue;
		}
		/* skip 4 bytes time stamp packet */
		if (log_hdr->tag == EVENT_LOG_TAG_TS) {
			datalen -= log_hdr->count * 4 + hdrlen;
			log_hdr -= log_hdr->count + hdrlen / 4;
			continue;
		}
		if (!(log_item = MALLOC(dhdp->osh, sizeof(*log_item)))) {
			DHD_ERROR(("%s allocating log list item failed\n",
				__FUNCTION__));
			break;
		}
		log_item->hdr = log_hdr;
		dll_insert(&log_item->list, &list_head);
		datalen -= (log_hdr->count * 4 + hdrlen);
	}

	while (!dll_empty(&list_head)) {
		msg_processed = FALSE;
		cur = dll_head_p(&list_head);
		log_item = (loglist_item_t *)container_of(cur, loglist_item_t, list);
		log_hdr = log_item->hdr;
		log_ptr = (uint32 *)log_hdr - log_hdr->count;
		dll_delete(cur);
		MFREE(dhdp->osh, log_item, sizeof(*log_item));

		/* Before DHD debugability is implemented WLC_E_TRACE had been
		 * used to carry verbose logging from firmware. We need to
		 * be able to handle those messages even without a initialized
		 * debug layer.
		 */
		if (dhdp->dbg) {
			/* check the data for NAN event ring; keeping first as small table */
			/* process only user configured to log */
			nan_evt_ring_log_level = dhdp->dbg->dbg_rings[NAN_EVENT_RING_ID].log_level;
			if (dhdp->dbg->dbg_rings[NAN_EVENT_RING_ID].log_level) {
				for (id = 0; id < ARRAYSIZE(nan_event_level_map); id++) {
					if (nan_event_level_map[id].tag == log_hdr->tag) {
						/* dont process if tag log level is greater
						 * than ring log level
						 */
						if (nan_event_level_map[id].log_level >
							nan_evt_ring_log_level) {
							msg_processed = TRUE;
							break;
						}
						/* In case of BCME_VERSION error,
						 * this is not NAN event type data
						 */
						if (dhd_dbg_nan_event_handler(dhdp,
							log_hdr, log_ptr) != BCME_VERSION) {
							msg_processed = TRUE;
						}
						break;
					}
				}
			}
			if (!msg_processed) {
				/* check the data for event ring */
				for (id = 0; id < ARRAYSIZE(fw_event_level_map); id++) {
					if (fw_event_level_map[id].tag == log_hdr->tag) {
						/* In case of BCME_VERSION error,
						 * this is not event type data
						 */
						if (dhd_dbg_custom_evnt_handler(dhdp,
							log_hdr, log_ptr) != BCME_VERSION) {
							msg_processed = TRUE;
						}
						break;
					}
				}
			}
		}
		if (!msg_processed)
			dhd_dbg_verboselog_handler(dhdp, log_hdr, raw_event_ptr);

	}
}
#else /* !SHOW_LOGTRACE */
static INLINE void dhd_dbg_verboselog_handler(dhd_pub_t *dhdp,
	event_log_hdr_t *hdr, void *raw_event_ptr) {};
static INLINE void dhd_dbg_msgtrace_log_parser(dhd_pub_t *dhdp,
	void *event_data, void *raw_event_ptr, uint datalen) {};
#endif /* SHOW_LOGTRACE */

void
dhd_dbg_trace_evnt_handler(dhd_pub_t *dhdp, void *event_data,
		void *raw_event_ptr, uint datalen)
{
	msgtrace_hdr_t *hdr;

	hdr = (msgtrace_hdr_t *)event_data;

	if (hdr->version != MSGTRACE_VERSION) {
		DHD_DBGIF(("%s unsupported MSGTRACE version, dhd %d, dongle %d\n",
			__FUNCTION__, MSGTRACE_VERSION, hdr->version));
		return;
	}

	if (hdr->trace_type == MSGTRACE_HDR_TYPE_MSG)
		dhd_dbg_msgtrace_msg_parser(event_data);
	else if (hdr->trace_type == MSGTRACE_HDR_TYPE_LOG)
		dhd_dbg_msgtrace_log_parser(dhdp, event_data, raw_event_ptr, datalen);
}

static int
dhd_dbg_ring_init(dhd_pub_t *dhdp, dhd_dbg_ring_t *ring, uint16 id, uint8 *name,
		uint32 ring_sz)
{
	void *buf;
	unsigned long flags;

	buf = MALLOCZ(dhdp->osh, ring_sz);
	if (!buf)
		return BCME_NOMEM;

	ring->lock = dhd_os_spin_lock_init(dhdp->osh);

	flags = dhd_os_spin_lock(ring->lock);
	ring->id = id;
	strncpy(ring->name, name, DBGRING_NAME_MAX);
	ring->name[DBGRING_NAME_MAX - 1] = 0;
	ring->ring_size = ring_sz;
	ring->wp = ring->rp = 0;
	ring->ring_buf = buf;
	ring->threshold = DBGRING_FLUSH_THRESHOLD(ring);
	ring->state = RING_SUSPEND;
	dhd_os_spin_unlock(ring->lock, flags);

	return BCME_OK;
}

static void
dhd_dbg_ring_deinit(dhd_pub_t *dhdp, dhd_dbg_ring_t *ring)
{
	void *buf;
	uint32 ring_sz;
	unsigned long flags;

	if (!ring->ring_buf)
		return;

	flags = dhd_os_spin_lock(ring->lock);
	ring->id = 0;
	ring->name[0] = 0;
	ring_sz = ring->ring_size;
	ring->ring_size = 0;
	ring->wp = ring->rp = 0;
	buf = ring->ring_buf;
	ring->ring_buf = NULL;
	memset(&ring->stat, 0, sizeof(ring->stat));
	ring->threshold = 0;
	ring->state = RING_STOP;
	dhd_os_spin_unlock(ring->lock, flags);

	dhd_os_spin_lock_deinit(dhdp->osh, ring->lock);

	MFREE(dhdp->osh, buf, ring_sz);
}

/*
 * dhd_dbg_set_event_log_tag : modify the state of an event log tag
 */
void
dhd_dbg_set_event_log_tag(dhd_pub_t *dhdp, uint16 tag, uint8 set)
{
	wl_el_tag_params_t pars;
	char *cmd = "event_log_tag_control";
	char iovbuf[WLC_IOCTL_SMLEN] = { 0 };
	int ret;

	memset(&pars, 0, sizeof(pars));
	pars.tag = tag;
	pars.set = set;
	pars.flags = EVENT_LOG_TAG_FLAG_LOG;

	if (!bcm_mkiovar(cmd, (char *)&pars, sizeof(pars), iovbuf, sizeof(iovbuf))) {
		DHD_ERROR(("%s mkiovar failed\n", __FUNCTION__));
		return;
	}

	ret = dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	if (ret) {
		DHD_ERROR(("%s set log tag iovar failed %d\n", __FUNCTION__, ret));
	}
}

int
dhd_dbg_set_configuration(dhd_pub_t *dhdp, int ring_id, int log_level, int flags, uint32 threshold)
{
	dhd_dbg_ring_t *ring;
	uint8 set = 1;
	unsigned long lock_flags;
	int i, array_len = 0;
	struct log_level_table *log_level_tbl = NULL;
	if (!dhdp || !dhdp->dbg)
		return BCME_BADADDR;

	ring = &dhdp->dbg->dbg_rings[ring_id];

	if (ring->state == RING_STOP)
		return BCME_UNSUPPORTED;

	lock_flags = dhd_os_spin_lock(ring->lock);
	if (log_level == 0)
		ring->state = RING_SUSPEND;
	else
		ring->state = RING_ACTIVE;
	ring->log_level = log_level;

	ring->threshold = (threshold > ring->threshold) ? ring->ring_size : threshold;
	dhd_os_spin_unlock(ring->lock, lock_flags);
	if (log_level > 0)
		set = TRUE;

	if (ring->id == FW_EVENT_RING_ID) {
		log_level_tbl = fw_event_level_map;
		array_len = ARRAYSIZE(fw_event_level_map);
	} else if (ring->id == FW_VERBOSE_RING_ID) {
		log_level_tbl = fw_verbose_level_map;
		array_len = ARRAYSIZE(fw_verbose_level_map);
	} else if (ring->id == NAN_EVENT_RING_ID) {
		log_level_tbl = nan_event_level_map;
		array_len = ARRAYSIZE(nan_event_level_map);
	}

	for (i = 0; i < array_len; i++) {
		if (log_level == 0 || (log_level_tbl[i].log_level > log_level)) {
			/* clear the reference per ring */
			ref_tag_tbl[log_level_tbl[i].tag] &= ~(1 << ring_id);
		} else {
			/* set the reference per ring */
			ref_tag_tbl[log_level_tbl[i].tag] |= (1 << ring_id);
		}
		set = (ref_tag_tbl[log_level_tbl[i].tag])? 1 : 0;
		DHD_DBGIF(("%s TAG(%s) is %s for the ring(%s)\n", __FUNCTION__,
			log_level_tbl[i].desc, (set)? "SET" : "CLEAR", ring->name));
		dhd_dbg_set_event_log_tag(dhdp, log_level_tbl[i].tag, set);
	}
	return BCME_OK;
}

/*
* dhd_dbg_get_ring_status : get the ring status from the coresponding ring buffer
* Return: An error code or 0 on success.
*/

int
dhd_dbg_get_ring_status(dhd_pub_t *dhdp, int ring_id, dhd_dbg_ring_status_t *dbg_ring_status)
{
	int ret = BCME_OK;
	int id = 0;
	dhd_dbg_t *dbg;
	dhd_dbg_ring_t *dbg_ring;
	dhd_dbg_ring_status_t ring_status;
	if (!dhdp || !dhdp->dbg)
		return BCME_BADADDR;
	dbg = dhdp->dbg;

	memset(&ring_status, 0, sizeof(dhd_dbg_ring_status_t));
	for (id = DEBUG_RING_ID_INVALID + 1; id < DEBUG_RING_ID_MAX; id++) {
		dbg_ring = &dbg->dbg_rings[id];
		if (VALID_RING(dbg_ring->id) && (dbg_ring->id == ring_id)) {
			RING_STAT_TO_STATUS(dbg_ring, ring_status);
			*dbg_ring_status = ring_status;
			break;
		}
	}
	if (!VALID_RING(id)) {
		DHD_ERROR(("%s : cannot find the ring_id : %d\n", __FUNCTION__, ring_id));
		ret = BCME_NOTFOUND;
	}
	return ret;
}

/*
* dhd_dbg_find_ring_id : return ring_id based on ring_name
* Return: An invalid ring id for failure or valid ring id on success.
*/

int
dhd_dbg_find_ring_id(dhd_pub_t *dhdp, char *ring_name)
{
	int id;
	dhd_dbg_t *dbg;
	dhd_dbg_ring_t *ring;

	if (!dhdp || !dhdp->dbg)
		return BCME_BADADDR;

	dbg = dhdp->dbg;
	for (id = DEBUG_RING_ID_INVALID + 1; id < DEBUG_RING_ID_MAX; id++) {
		ring = &dbg->dbg_rings[id];
		if (!strncmp(ring->name, ring_name, sizeof(ring->name) - 1))
			break;
	}
	return id;
}

/*
* dhd_dbg_get_priv : get the private data of dhd dbugability module
* Return : An NULL on failure or valid data address
*/
void *
dhd_dbg_get_priv(dhd_pub_t *dhdp)
{
	if (!dhdp || !dhdp->dbg)
		return NULL;
	return dhdp->dbg->private;
}

/*
* dhd_dbg_start : start and stop All of Ring buffers
* Return: An error code or 0 on success.
*/
int
dhd_dbg_start(dhd_pub_t *dhdp, bool start)
{
	int ret = BCME_OK;
	int ring_id;
	dhd_dbg_t *dbg;
	dhd_dbg_ring_t *dbg_ring;
	if (!dhdp || !dhdp->dbg)
		return BCME_BADARG;
	dbg = dhdp->dbg;

	for (ring_id = DEBUG_RING_ID_INVALID + 1; ring_id < DEBUG_RING_ID_MAX; ring_id++) {
		dbg_ring = &dbg->dbg_rings[ring_id];
		if (!start) {
			if (VALID_RING(dbg_ring->id)) {
				/* Initialize the information for the ring */
				dbg_ring->state = RING_SUSPEND;
				dbg_ring->log_level = 0;
				dbg_ring->rp = dbg_ring->wp = 0;
				dbg_ring->threshold = 0;
				memset(&dbg_ring->stat, 0, sizeof(struct ring_statistics));
				memset(dbg_ring->ring_buf, 0, dbg_ring->ring_size);
			}
		}
	}
	return ret;
}

/*
 * dhd_dbg_send_urgent_evt: send the health check evt to Upper layer
 *
 * Return: An error code or 0 on success.
 */

int
dhd_dbg_send_urgent_evt(dhd_pub_t *dhdp, const void *data, const uint32 len)
{
	dhd_dbg_t *dbg;
	int ret = BCME_OK;
	if (!dhdp || !dhdp->dbg)
		return BCME_BADADDR;

	dbg = dhdp->dbg;
	if (dbg->urgent_notifier) {
		dbg->urgent_notifier(dhdp, data, len);
	}
	return ret;
}
/*
 * dhd_dbg_attach: initialziation of dhd dbugability module
 *
 * Return: An error code or 0 on success.
 */
int
dhd_dbg_attach(dhd_pub_t *dhdp, dbg_pullreq_t os_pullreq,
	dbg_urgent_noti_t os_urgent_notifier, void *os_priv)
{
	dhd_dbg_t *dbg;
	int ret, ring_id;

	dbg = MALLOCZ(dhdp->osh, sizeof(dhd_dbg_t));
	if (!dbg)
		return BCME_NOMEM;

	ret = dhd_dbg_ring_init(dhdp, &dbg->dbg_rings[FW_VERBOSE_RING_ID], FW_VERBOSE_RING_ID,
			FW_VERBOSE_RING_NAME, FW_VERBOSE_RING_SIZE);
	if (ret)
		goto error;

	ret = dhd_dbg_ring_init(dhdp, &dbg->dbg_rings[FW_EVENT_RING_ID], FW_EVENT_RING_ID,
			FW_EVENT_RING_NAME, FW_EVENT_RING_SIZE);
	if (ret)
		goto error;

	ret = dhd_dbg_ring_init(dhdp, &dbg->dbg_rings[DHD_EVENT_RING_ID], DHD_EVENT_RING_ID,
			DHD_EVENT_RING_NAME, DHD_EVENT_RING_SIZE);
	if (ret)
		goto error;

	ret = dhd_dbg_ring_init(dhdp, &dbg->dbg_rings[NAN_EVENT_RING_ID], NAN_EVENT_RING_ID,
			NAN_EVENT_RING_NAME, NAN_EVENT_RING_SIZE);
	if (ret)
		goto error;

	dbg->private = os_priv;
	dbg->pullreq = os_pullreq;
	dbg->urgent_notifier = os_urgent_notifier;
	dhdp->dbg = dbg;

	return BCME_OK;

error:
	for (ring_id = DEBUG_RING_ID_INVALID + 1; ring_id < DEBUG_RING_ID_MAX; ring_id++) {
		if (VALID_RING(dbg->dbg_rings[ring_id].id)) {
			dhd_dbg_ring_deinit(dhdp, &dbg->dbg_rings[ring_id]);
		}
	}
	MFREE(dhdp->osh, dhdp->dbg, sizeof(dhd_dbg_t));

	return ret;
}

/*
 * dhd_dbg_detach: clean up dhd dbugability module
 */
void
dhd_dbg_detach(dhd_pub_t *dhdp)
{
	int ring_id;
	dhd_dbg_t *dbg;
	if (!dhdp->dbg)
		return;
	dbg = dhdp->dbg;
	for (ring_id = DEBUG_RING_ID_INVALID + 1; ring_id < DEBUG_RING_ID_MAX; ring_id++) {
		if (VALID_RING(dbg->dbg_rings[ring_id].id)) {
			dhd_dbg_ring_deinit(dhdp, &dbg->dbg_rings[ring_id]);
		}
	}
	MFREE(dhdp->osh, dhdp->dbg, sizeof(dhd_dbg_t));
}
