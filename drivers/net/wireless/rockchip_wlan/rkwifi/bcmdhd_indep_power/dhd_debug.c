/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DHD debugability support
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 * $Id: dhd_debug.c 711908 2017-07-20 10:37:34Z $
 */

#include <typedefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <dhd_debug.h>
#include <dhd_mschdbg.h>

#include <event_log.h>
#include <event_trace.h>
#include <msgtrace.h>

#if defined(DHD_EFI)
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define container_of(ptr, type, member) \
		((type *)((char *)(ptr) - offsetof(type, member)))
#endif 

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

#define DHD_PKT_INFO DHD_ERROR
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
	{1, EVENT_LOG_TAG_PCI_ERROR, EVENT_LOG_SET_BUS, "PCI_ERROR"},
	{1, EVENT_LOG_TAG_PCI_WARN, EVENT_LOG_SET_BUS, "PCI_WARN"},
	{2, EVENT_LOG_TAG_PCI_INFO, EVENT_LOG_SET_BUS, "PCI_INFO"},
	{3, EVENT_LOG_TAG_PCI_DBG, EVENT_LOG_SET_BUS, "PCI_DEBUG"},
	{3, EVENT_LOG_TAG_BEACON_LOG, EVENT_LOG_SET_WL, "BEACON_LOG"},
	{2, EVENT_LOG_TAG_WL_ASSOC_LOG, EVENT_LOG_SET_WL, "ASSOC_LOG"},
	{2, EVENT_LOG_TAG_WL_ROAM_LOG, EVENT_LOG_SET_WL, "ROAM_LOG"},
	{1, EVENT_LOG_TAG_TRACE_WL_INFO, EVENT_LOG_SET_WL, "WL_INFO"},
	{1, EVENT_LOG_TAG_TRACE_BTCOEX_INFO, EVENT_LOG_SET_WL, "BTCOEX_INFO"},
#ifdef CUSTOMER_HW4_DEBUG
	{3, EVENT_LOG_TAG_SCAN_WARN, EVENT_LOG_SET_WL, "SCAN_WARN"},
#else
	{1, EVENT_LOG_TAG_SCAN_WARN, EVENT_LOG_SET_WL, "SCAN_WARN"},
#endif /* CUSTOMER_HW4_DEBUG */
	{1, EVENT_LOG_TAG_SCAN_ERROR, EVENT_LOG_SET_WL, "SCAN_ERROR"},
	{2, EVENT_LOG_TAG_SCAN_TRACE_LOW, EVENT_LOG_SET_WL, "SCAN_TRACE_LOW"},
	{2, EVENT_LOG_TAG_SCAN_TRACE_HIGH, EVENT_LOG_SET_WL, "SCAN_TRACE_HIGH"}
};

struct log_level_table fw_event_level_map[] = {
	{1, EVENT_LOG_TAG_TRACE_WL_INFO, EVENT_LOG_SET_WL, "WL_INFO"},
	{1, EVENT_LOG_TAG_TRACE_BTCOEX_INFO, EVENT_LOG_SET_WL, "BTCOEX_INFO"},
#ifdef CUSTOMER_HW4_DEBUG
	{3, EVENT_LOG_TAG_BEACON_LOG, EVENT_LOG_SET_WL, "BEACON LOG"},
#else
	{2, EVENT_LOG_TAG_BEACON_LOG, EVENT_LOG_SET_WL, "BEACON LOG"},
#endif /* CUSTOMER_HW4_DEBUG */
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
	{1, EVENT_LOG_TAG_NAN_ERROR, 0, "NAN_ERROR"},
	{2, EVENT_LOG_TAG_NAN_INFO, 0, "NAN_INFO"},
	{3, EVENT_LOG_TAG_NAN_DBG, 0, "NAN_DEBUG"},
};

struct map_table nan_evt_tag_map[] = {
	{TRACE_TAG_BSSID, WIFI_TAG_BSSID, "BSSID"},
	{TRACE_TAG_ADDR, WIFI_TAG_ADDR, "ADDR_0"},
};

/* reference tab table */
uint ref_tag_tbl[EVENT_LOG_TAG_MAX + 1] = {0};

typedef struct dhddbg_loglist_item {
	dll_t list;
	event_log_hdr_t *hdr;
} loglist_item_t;

typedef struct dhbdbg_pending_item {
	dll_t list;
	dhd_dbg_ring_status_t ring_status;
	dhd_dbg_ring_entry_t *ring_entry;
} pending_item_t;

/* trace log entry header user space processing */
struct tracelog_header {
	int magic_num;
	int buf_size;
	int seq_num;
};
#define TRACE_LOG_MAGIC_NUMBER 0xEAE47C06

int
dhd_dbg_ring_pull_single(dhd_pub_t *dhdp, int ring_id, void *data, uint32 buf_len,
	bool strip_header)
{
	dhd_dbg_ring_t *ring;
	dhd_dbg_ring_entry_t *r_entry;
	uint32 rlen;
	char *buf;

	if (!dhdp || !dhdp->dbg) {
		return 0;
	}

	ring = &dhdp->dbg->dbg_rings[ring_id];

	if (ring->state != RING_ACTIVE) {
		return 0;
	}

	if (ring->rp == ring->wp) {
		return 0;
	}

	r_entry = (dhd_dbg_ring_entry_t *)((uint8 *)ring->ring_buf + ring->rp);

	/* Boundary Check */
	rlen = ENTRY_LENGTH(r_entry);
	if ((ring->rp + rlen) > ring->ring_size) {
		DHD_ERROR(("%s: entry len %d is out of boundary of ring size %d,"
			" current ring %d[%s] - rp=%d\n", __FUNCTION__, rlen,
			ring->ring_size, ring->id, ring->name, ring->rp));
		return 0;
	}

	if (strip_header) {
		rlen = r_entry->len;
		buf = (char *)r_entry + DBG_RING_ENTRY_SIZE;
	} else {
		rlen = ENTRY_LENGTH(r_entry);
		buf = (char *)r_entry;
	}
	if (rlen > buf_len) {
		DHD_ERROR(("%s: buf len %d is too small for entry len %d\n",
			__FUNCTION__, buf_len, rlen));
		DHD_ERROR(("%s: ring %d[%s] - ring size=%d, wp=%d, rp=%d\n",
			__FUNCTION__, ring->id, ring->name, ring->ring_size,
			ring->wp, ring->rp));
		ASSERT(0);
		return 0;
	}

	memcpy(data, buf, rlen);
	/* update ring context */
	ring->rp += ENTRY_LENGTH(r_entry);
	/* skip padding if there is one */
	if (ring->tail_padded && ((ring->rp + ring->rem_len) == ring->ring_size)) {
		DHD_DBGIF(("%s: RING%d[%s] Found padding, rp=%d, wp=%d\n",
			__FUNCTION__, ring->id, ring->name, ring->rp, ring->wp));
		ring->rp = 0;
		ring->tail_padded = FALSE;
		ring->rem_len = 0;
	}
	if (ring->rp >= ring->ring_size) {
		DHD_ERROR(("%s: RING%d[%s] rp pointed out of ring boundary,"
			" rp=%d, ring_size=%d\n", __FUNCTION__, ring->id,
			ring->name, ring->rp, ring->ring_size));
		ASSERT(0);
	}
	ring->stat.read_bytes += ENTRY_LENGTH(r_entry);
	DHD_DBGIF(("%s RING%d[%s]read_bytes %d, wp=%d, rp=%d\n", __FUNCTION__,
		ring->id, ring->name, ring->stat.read_bytes, ring->wp, ring->rp));

	return rlen;
}

int
dhd_dbg_ring_pull(dhd_pub_t *dhdp, int ring_id, void *data, uint32 buf_len)
{
	int32 r_len, total_r_len = 0;
	dhd_dbg_ring_t *ring;

	if (!dhdp || !dhdp->dbg)
		return 0;
	ring = &dhdp->dbg->dbg_rings[ring_id];
	if (ring->state != RING_ACTIVE)
		return 0;

	while (buf_len > 0) {
		r_len = dhd_dbg_ring_pull_single(dhdp, ring_id, data, buf_len, FALSE);
		if (r_len == 0)
			break;
		data = (uint8 *)data + r_len;
		buf_len -= r_len;
		total_r_len += r_len;
	}

	return total_r_len;
}

int
dhd_dbg_ring_push(dhd_pub_t *dhdp, int ring_id, dhd_dbg_ring_entry_t *hdr, void *data)
{
	unsigned long flags;
	uint32 pending_len;
	uint32 w_len;
	uint32 avail_size;
	dhd_dbg_ring_t *ring;
	dhd_dbg_ring_entry_t *w_entry, *r_entry;

	if (!dhdp || !dhdp->dbg) {
		return BCME_BADADDR;
	}

	ring = &dhdp->dbg->dbg_rings[ring_id];

	if (ring->state != RING_ACTIVE) {
		return BCME_OK;
	}

	flags = dhd_os_spin_lock(ring->lock);

	w_len = ENTRY_LENGTH(hdr);

	if (w_len > ring->ring_size) {
		dhd_os_spin_unlock(ring->lock, flags);
		return BCME_ERROR;
	}

	/* Claim the space */
	do {
		avail_size = DBG_RING_CHECK_WRITE_SPACE(ring->rp, ring->wp, ring->ring_size);
		if (avail_size <= w_len) {
			/* Prepare the space */
			if (ring->rp <= ring->wp) {
				ring->tail_padded = TRUE;
				ring->rem_len = ring->ring_size - ring->wp;
				DHD_DBGIF(("%s: RING%d[%s] Insuffient tail space,"
					" rp=%d, wp=%d, rem_len=%d, ring_size=%d,"
					" avail_size=%d, w_len=%d\n", __FUNCTION__,
					ring->id, ring->name, ring->rp, ring->wp,
					ring->rem_len, ring->ring_size, avail_size,
					w_len));

				/* 0 pad insufficient tail space */
				memset((uint8 *)ring->ring_buf + ring->wp, 0, ring->rem_len);
				if (ring->rp == ring->wp) {
					ring->rp = 0;
				}
				ring->wp = 0;
			} else {
				/* Not enough space for new entry, free some up */
				r_entry = (dhd_dbg_ring_entry_t *)((uint8 *)ring->ring_buf +
					ring->rp);
				ring->rp += ENTRY_LENGTH(r_entry);
				/* skip padding if there is one */
				if (ring->tail_padded &&
					((ring->rp + ring->rem_len) == ring->ring_size)) {
					DHD_DBGIF(("%s: RING%d[%s] Found padding,"
						" avail_size=%d, w_len=%d\n", __FUNCTION__,
						ring->id, ring->name, avail_size, w_len));
					ring->rp = 0;
					ring->tail_padded = FALSE;
					ring->rem_len = 0;
				}
				if (ring->rp >= ring->ring_size) {
					DHD_ERROR(("%s: RING%d[%s] rp points out of boundary,"
						" ring->rp = %d, ring->ring_size=%d\n",
						__FUNCTION__, ring->id, ring->name, ring->rp,
						ring->ring_size));
					ASSERT(0);
				}
				ring->stat.read_bytes += ENTRY_LENGTH(r_entry);
				DHD_DBGIF(("%s: RING%d[%s] read_bytes  %d, wp=%d, rp=%d\n",
					__FUNCTION__, ring->id, ring->name, ring->stat.read_bytes,
					ring->wp, ring->rp));
			}
		} else {
			break;
		}
	} while (TRUE);

	w_entry = (dhd_dbg_ring_entry_t *)((uint8 *)ring->ring_buf + ring->wp);
	/* header */
	memcpy(w_entry, hdr, DBG_RING_ENTRY_SIZE);
	w_entry->len = hdr->len;
	/* payload */
	memcpy((char *)w_entry + DBG_RING_ENTRY_SIZE, data, w_entry->len);
	/* update write pointer */
	ring->wp += w_len;
	if (ring->wp >= ring->ring_size) {
		DHD_ERROR(("%s: RING%d[%s] wp pointed out of ring boundary, "
			"wp=%d, ring_size=%d\n", __FUNCTION__, ring->id,
			ring->name, ring->wp, ring->ring_size));
		ASSERT(0);
	}
	/* update statistics */
	ring->stat.written_records++;
	ring->stat.written_bytes += w_len;
	DHD_DBGIF(("%s : RING%d[%s] written_records %d, written_bytes %d, read_bytes=%d,"
		" ring->threshold=%d, wp=%d, rp=%d\n", __FUNCTION__, ring->id, ring->name,
		ring->stat.written_records, ring->stat.written_bytes, ring->stat.read_bytes,
		ring->threshold, ring->wp, ring->rp));

	/* Calculate current pending size */
	if (ring->stat.written_bytes > ring->stat.read_bytes) {
		pending_len = ring->stat.written_bytes - ring->stat.read_bytes;
	} else if (ring->stat.written_bytes < ring->stat.read_bytes) {
		pending_len = 0xFFFFFFFF - ring->stat.read_bytes + ring->stat.written_bytes;
	} else {
		pending_len = 0;
	}

	/* if the current pending size is bigger than threshold */
	if (ring->threshold > 0 &&
	   (pending_len >= ring->threshold) && ring->sched_pull) {
		dhdp->dbg->pullreq(dhdp->dbg->private, ring->id);
		ring->sched_pull = FALSE;
	}
	dhd_os_spin_unlock(ring->lock, flags);
	return BCME_OK;
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

#ifndef MACOSX_DHD
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
#endif /* MACOSX_DHD */
#ifdef SHOW_LOGTRACE
static const uint8 *
event_get_tlv(uint16 id, const char* tlvs, uint tlvs_len)
{
	const uint8 *pos = (const uint8 *)tlvs;
	const uint8 *end = pos + tlvs_len;
	const tlv_log *tlv;
	int rest;

	while (pos + 1 < end) {
		if (pos + 4 + pos[1] > end)
			break;
		tlv = (const tlv_log *) pos;
		if (tlv->tag == id)
			return pos;
		rest = tlv->len % 4; /* padding values */
		pos += 4 + tlv->len + rest;
	}
	return NULL;
}

#define DATA_UNIT_FOR_LOG_CNT 4
/* #pragma used as a WAR to fix build failure,
 * ignore dropping of 'const' qualifier in tlv_data assignment
 * this pragma disables the warning only for the following function
 */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
static int
dhd_dbg_nan_event_handler(dhd_pub_t *dhdp, event_log_hdr_t *hdr, uint32 *data)
{
	int ret = BCME_OK;
	wl_event_log_id_ver_t nan_hdr;
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

	/* nan event log should at least contain a wl_event_log_id_ver_t
	 * header and a arm cycle count
	 */
	if (hdr->count < NAN_EVENT_LOG_MIN_LENGTH) {
		return BCME_BADLEN;
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
	wl_event_log_id_ver_t wl_log_id;
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

	/* custom event log should at least contain a wl_event_log_id_ver_t
	 * header and a arm cycle count
	 */
	if (hdr->count < NAN_EVENT_LOG_MIN_LENGTH) {
		return BCME_BADLEN;
	}

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
			tlv_data = (tlv_log *)event_get_tlv(event_tag_map[i].fw_id,
			         tlvs, tlv_len);
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
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* To identify format of types %Ns where N >= 0 is a number */
bool
check_valid_string_format(char *curr_ptr)
{
	char *next_ptr;
	if ((next_ptr = bcmstrstr(curr_ptr, "s")) != NULL) {
		/* Default %s format */
		if (curr_ptr == next_ptr) {
			return TRUE;
		}

		/* Verify each charater between '%' and 's' is a valid number */
		while (curr_ptr < next_ptr) {
			if (bcm_isdigit(*curr_ptr) == FALSE) {
				return FALSE;
			}
			curr_ptr++;
		}

		return TRUE;
	} else {
		return FALSE;
	}
}

#define MAX_NO_OF_ARG	16
#define FMTSTR_SIZE	132
#define ROMSTR_SIZE	200
#define SIZE_LOC_STR	50
static uint64 verboselog_ts_saved = 0;
static void
dhd_dbg_verboselog_handler(dhd_pub_t *dhdp, event_log_hdr_t *hdr,
		void *raw_event_ptr)
{
	event_log_hdr_t *ts_hdr;
	uint32 *log_ptr = (uint32 *)hdr - hdr->count;
	char fmtstr_loc_buf[ROMSTR_SIZE] = { 0 };
	uint32 rom_str_len = 0;
	uint32 *ts_data;

	if (!raw_event_ptr) {
		return;
	}

	/* Get time stamp if it's updated */
	ts_hdr = (event_log_hdr_t *)((char *)log_ptr - sizeof(event_log_hdr_t));
	if (ts_hdr->tag == EVENT_LOG_TAG_TS) {
		ts_data = (uint32 *)ts_hdr - ts_hdr->count;
		verboselog_ts_saved = (uint64)ts_data[0];
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

	if (hdr->tag == EVENT_LOG_TAG_MSCHPROFILE || hdr->tag == EVENT_LOG_TAG_MSCHPROFILE_TLV) {
		wl_mschdbg_verboselog_handler(dhdp, raw_event_ptr, hdr->tag, log_ptr);
		return;
	}

	/* print the message out in a logprint  */
	dhd_dbg_verboselog_printf(dhdp, hdr, raw_event_ptr, log_ptr);
}

void
dhd_dbg_verboselog_printf(dhd_pub_t *dhdp, event_log_hdr_t *hdr,
	void *raw_event_ptr, uint32 *log_ptr)
{
	dhd_event_log_t *raw_event = (dhd_event_log_t *)raw_event_ptr;
	uint16 count;
	int log_level, id;
	char fmtstr_loc_buf[ROMSTR_SIZE] = { 0 };
	char (*str_buf)[SIZE_LOC_STR] = NULL;
	char *str_tmpptr = NULL;
	uint32 addr = 0;
	typedef union {
		uint32 val;
		char * addr;
	} u_arg;
	u_arg arg[MAX_NO_OF_ARG] = {{0}};
	char *c_ptr = NULL;

	BCM_REFERENCE(arg);

	if (!raw_event) {
		return;
	}

	/* print the message out in a logprint  */
	if (!(raw_event->fmts) || hdr->fmt_num == 0xffff) {
		if (dhdp->dbg) {
			log_level = dhdp->dbg->dbg_rings[FW_VERBOSE_RING_ID].log_level;
			for (id = 0; id < ARRAYSIZE(fw_verbose_level_map); id++) {
				if ((fw_verbose_level_map[id].tag == hdr->tag) &&
					(fw_verbose_level_map[id].log_level > log_level))
					return;
			}
		}

		DHD_EVENT(("%d.%d EL:tag=%d len=%d fmt=0x%x",
			(uint32)verboselog_ts_saved / 1000,
			(uint32)verboselog_ts_saved % 1000,
			hdr->tag,
			hdr->count,
			hdr->fmt_num));

		for (count = 0; count < (hdr->count-1); count++) {
			if (count % 8 == 0)
				DHD_EVENT(("\n\t%08x", log_ptr[count]));
			else
				DHD_EVENT((" %08x", log_ptr[count]));
		}
		DHD_EVENT(("\n"));

		return;
	}

	str_buf = MALLOCZ(dhdp->osh, (MAX_NO_OF_ARG * SIZE_LOC_STR));
	if (!str_buf) {
		DHD_ERROR(("%s: malloc failed str_buf\n", __FUNCTION__));
		return;
	}

	if ((hdr->fmt_num >> 2) < raw_event->num_fmts) {
		if (hdr->tag == EVENT_LOG_TAG_MSCHPROFILE) {
			snprintf(fmtstr_loc_buf, FMTSTR_SIZE, "%s",
				raw_event->fmts[hdr->fmt_num >> 2]);
			hdr->count++;
		} else {
			snprintf(fmtstr_loc_buf, FMTSTR_SIZE, "CONSOLE_E: %6d.%3d %s",
				log_ptr[hdr->count-1]/1000, (log_ptr[hdr->count - 1] % 1000),
				raw_event->fmts[hdr->fmt_num >> 2]);
		}
		c_ptr = fmtstr_loc_buf;
	} else {
		DHD_ERROR(("%s: fmt number out of range \n", __FUNCTION__));
		goto exit;
	}

	for (count = 0; count < (hdr->count - 1); count++) {
		if (c_ptr != NULL)
			if ((c_ptr = bcmstrstr(c_ptr, "%")) != NULL)
				c_ptr++;

		if (c_ptr != NULL) {
			if (check_valid_string_format(c_ptr)) {
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
		}
	}

	/* Print FW logs */
	DHD_FWLOG((fmtstr_loc_buf, arg[0], arg[1], arg[2], arg[3],
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
	uint32 log_hdr_len = sizeof(event_log_hdr_t);
	uint32 log_pyld_len;
	static uint32 seqnum_prev = 0;
	event_log_hdr_t *log_hdr;
	bool msg_processed = FALSE;
	uint32 *log_ptr =  NULL;
	dll_t list_head, *cur;
	loglist_item_t *log_item;
	int32 nan_evt_ring_log_level = 0;
	dhd_dbg_ring_entry_t msg_hdr;
	char *logbuf;
	struct tracelog_header *logentry_header;

	/* log trace event consists of:
	 * msgtrace header
	 * event log block header
	 * event log payload
	 */
	if (datalen <= MSGTRACE_HDRLEN + EVENT_LOG_BLOCK_HDRLEN) {
		return;
	}
	hdr = (msgtrace_hdr_t *)event_data;
	data = (char *)event_data + MSGTRACE_HDRLEN;
	datalen -= MSGTRACE_HDRLEN;

	if (dhd_dbg_msgtrace_seqchk(&seqnum_prev, ntoh32(hdr->seqnum)))
		return;

	/* Save the whole message to event log ring */
	memset(&msg_hdr, 0, sizeof(dhd_dbg_ring_entry_t));
	logbuf = VMALLOC(dhdp->osh, sizeof(*logentry_header) + datalen);
	if (logbuf == NULL)
		return;
	logentry_header = (struct tracelog_header *)logbuf;
	logentry_header->magic_num = TRACE_LOG_MAGIC_NUMBER;
	logentry_header->buf_size = datalen;
	logentry_header->seq_num = hdr->seqnum;
	msg_hdr.type = DBG_RING_ENTRY_DATA_TYPE;

	if ((sizeof(*logentry_header) + datalen) > PAYLOAD_MAX_LEN) {
		DHD_ERROR(("%s:Payload len=%u exceeds max len\n", __FUNCTION__,
			((uint)sizeof(*logentry_header) + datalen)));
		VMFREE(dhdp->osh, logbuf, sizeof(*logentry_header) + datalen);
		return;
	}

	msg_hdr.len = sizeof(*logentry_header) + datalen;
	memcpy(logbuf + sizeof(*logentry_header), data, datalen);
	dhd_dbg_ring_push(dhdp, FW_VERBOSE_RING_ID, &msg_hdr, logbuf);
	VMFREE(dhdp->osh, logbuf, sizeof(*logentry_header) + datalen);

	/* Print sequence number, originating set and length of received
	 * event log buffer. Refer to event log buffer structure in
	 * event_log.h
	 */
	DHD_MSGTRACE_LOG(("EVENT_LOG_HDR[0x%x]: Set: 0x%08x length = %d\n",
		ltoh16(*((uint16 *)(data+2))), ltoh32(*((uint32 *)(data + 4))),
		ltoh16(*((uint16 *)(data)))));
	data += EVENT_LOG_BLOCK_HDRLEN;
	datalen -= EVENT_LOG_BLOCK_HDRLEN;

	/* start parsing from the tail of packet
	 * Sameple format of a meessage
	 * 001d3c54 00000064 00000064 001d3c54 001dba08 035d6ce1 0c540639
	 * 001d3c54 00000064 00000064 035d6d89 0c580439
	 * 0x0c580439 -- 39 is tag, 04 is count, 580c is format number
	 * all these uint32 values comes in reverse order as group as EL data
	 * while decoding we can only parse from last to first
	 * |<-                     datalen                     ->|
	 * |----(payload and maybe more logs)----|event_log_hdr_t|
	 * data                                  log_hdr
	 */
	dll_init(&list_head);
	while (datalen > log_hdr_len) {
		log_hdr = (event_log_hdr_t *)(data + datalen - log_hdr_len);
		/* skip zero padding at end of frame */
		if (log_hdr->tag == EVENT_LOG_TAG_NULL) {
			datalen -= log_hdr_len;
			continue;
		}
		/* Check argument count, any event log should contain at least
		 * one argument (4 bytes) for arm cycle count and up to 16
		 * arguments when the format is valid
		 */
		if (log_hdr->count == 0) {
			break;
		}
		if ((log_hdr->count > MAX_NO_OF_ARG) && (log_hdr->fmt_num != 0xffff)) {
			break;
		}

		log_pyld_len = log_hdr->count * DATA_UNIT_FOR_LOG_CNT;
		/* log data should not cross the event data boundary */
		if ((char *)log_hdr - data < log_pyld_len)
			break;
		/* skip 4 bytes time stamp packet */
		if (log_hdr->tag == EVENT_LOG_TAG_TS) {
			datalen -= log_pyld_len + log_hdr_len;
			continue;
		}
		if (!(log_item = MALLOC(dhdp->osh, sizeof(*log_item)))) {
			DHD_ERROR(("%s allocating log list item failed\n",
				__FUNCTION__));
			break;
		}
		log_item->hdr = log_hdr;
		dll_insert(&log_item->list, &list_head);
		datalen -= (log_pyld_len + log_hdr_len);
	}

	while (!dll_empty(&list_head)) {
		msg_processed = FALSE;
		cur = dll_head_p(&list_head);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
		log_item = (loglist_item_t *)container_of(cur, loglist_item_t, list);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
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
#ifndef MACOSX_DHD
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
#endif /* MACOSX_DHD */
static int
dhd_dbg_ring_init(dhd_pub_t *dhdp, dhd_dbg_ring_t *ring, uint16 id, uint8 *name,
		uint32 ring_sz, int section)
{
	void *buf;
	unsigned long flags;
#ifdef CONFIG_DHD_USE_STATIC_BUF
	buf = DHD_OS_PREALLOC(dhdp, section, ring_sz);
#else
	buf = MALLOCZ(dhdp->osh, ring_sz);
#endif
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
	ring->sched_pull = TRUE;
	ring->rem_len = 0;
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
#ifndef CONFIG_DHD_USE_STATIC_BUF
	MFREE(dhdp->osh, buf, ring_sz);
#endif
}

uint8
dhd_dbg_find_sets_by_tag(uint16 tag)
{
	uint i;
	uint8 sets = 0;

	for (i = 0; i < ARRAYSIZE(fw_verbose_level_map); i++) {
		if (fw_verbose_level_map[i].tag == tag) {
			sets |= fw_verbose_level_map[i].sets;
		}
	}

	for (i = 0; i < ARRAYSIZE(fw_event_level_map); i++) {
		if (fw_event_level_map[i].tag == tag) {
			sets |= fw_event_level_map[i].sets;
		}
	}

	return sets;
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
	pars.set = dhd_dbg_find_sets_by_tag(tag);
	pars.flags = set ? EVENT_LOG_TAG_FLAG_LOG : EVENT_LOG_TAG_FLAG_NONE;

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

	ring->threshold = MIN(threshold, DBGRING_FLUSH_THRESHOLD(ring));
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
		if (!strncmp((char *)ring->name, ring_name, sizeof(ring->name) - 1))
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
	if (!dhdp)
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

#if defined(DBG_PKT_MON) || defined(DHD_PKT_LOGGING)
uint32
__dhd_dbg_pkt_hash(uintptr_t pkt, uint32 pktid)
{
	uint32 __pkt;
	uint32 __pktid;

	__pkt = ((int)pkt) >= 0 ? (2 * pkt) : (-2 * pkt - 1);
	__pktid = ((int)pktid) >= 0 ? (2 * pktid) : (-2 * pktid - 1);

	return (__pkt >= __pktid ? (__pkt * __pkt + __pkt + __pktid) :
			(__pkt + __pktid * __pktid));
}

#define __TIMESPEC_TO_US(ts) \
	(((uint32)(ts).tv_sec * USEC_PER_SEC) + ((ts).tv_nsec / NSEC_PER_USEC))

uint32
__dhd_dbg_driver_ts_usec(void)
{
	struct osl_timespec ts;

	osl_get_monotonic_boottime(&ts);
	return ((uint32)(__TIMESPEC_TO_US(ts)));
}

wifi_tx_packet_fate
__dhd_dbg_map_tx_status_to_pkt_fate(uint16 status)
{
	wifi_tx_packet_fate pkt_fate;

	switch (status) {
		case WLFC_CTL_PKTFLAG_DISCARD:
			pkt_fate = TX_PKT_FATE_ACKED;
			break;
		case WLFC_CTL_PKTFLAG_D11SUPPRESS:
			/* intensional fall through */
		case WLFC_CTL_PKTFLAG_WLSUPPRESS:
			pkt_fate = TX_PKT_FATE_FW_QUEUED;
			break;
		case WLFC_CTL_PKTFLAG_TOSSED_BYWLC:
			pkt_fate = TX_PKT_FATE_FW_DROP_INVALID;
			break;
		case WLFC_CTL_PKTFLAG_DISCARD_NOACK:
			pkt_fate = TX_PKT_FATE_SENT;
			break;
		default:
			pkt_fate = TX_PKT_FATE_FW_DROP_OTHER;
			break;
	}

	return pkt_fate;
}
#endif /* DBG_PKT_MON || DHD_PKT_LOGGING */

#ifdef DBG_PKT_MON
static int
__dhd_dbg_free_tx_pkts(dhd_pub_t *dhdp, dhd_dbg_tx_info_t *tx_pkts,
	uint16 pkt_count)
{
	uint16 count;

	DHD_PKT_INFO(("%s, %d\n", __FUNCTION__, __LINE__));
	count = 0;
	while ((count < pkt_count) && tx_pkts) {
		if (tx_pkts->info.pkt)
			PKTFREE(dhdp->osh, tx_pkts->info.pkt, TRUE);
		tx_pkts++;
		count++;
	}

	return BCME_OK;
}

static int
__dhd_dbg_free_rx_pkts(dhd_pub_t *dhdp, dhd_dbg_rx_info_t *rx_pkts,
	uint16 pkt_count)
{
	uint16 count;

	DHD_PKT_INFO(("%s, %d\n", __FUNCTION__, __LINE__));
	count = 0;
	while ((count < pkt_count) && rx_pkts) {
		if (rx_pkts->info.pkt)
			PKTFREE(dhdp->osh, rx_pkts->info.pkt, TRUE);
		rx_pkts++;
		count++;
	}

	return BCME_OK;
}

void
__dhd_dbg_dump_pkt_info(dhd_pub_t *dhdp, dhd_dbg_pkt_info_t *info)
{
	if (DHD_PKT_MON_DUMP_ON()) {
		DHD_PKT_MON(("payload type   = %d\n", info->payload_type));
		DHD_PKT_MON(("driver ts      = %u\n", info->driver_ts));
		DHD_PKT_MON(("firmware ts    = %u\n", info->firmware_ts));
		DHD_PKT_MON(("packet hash    = %u\n", info->pkt_hash));
		DHD_PKT_MON(("packet length  = %zu\n", info->pkt_len));
		DHD_PKT_MON(("packet address = %p\n", info->pkt));
		DHD_PKT_MON(("packet data    = \n"));
		if (DHD_PKT_MON_ON()) {
			prhex(NULL, PKTDATA(dhdp->osh, info->pkt), info->pkt_len);
		}
	}
}

void
__dhd_dbg_dump_tx_pkt_info(dhd_pub_t *dhdp, dhd_dbg_tx_info_t *tx_pkt,
	uint16 count)
{
	if (DHD_PKT_MON_DUMP_ON()) {
		DHD_PKT_MON(("\nTX (count: %d)\n", ++count));
		DHD_PKT_MON(("packet fate    = %d\n", tx_pkt->fate));
		__dhd_dbg_dump_pkt_info(dhdp, &tx_pkt->info);
	}
}

void
__dhd_dbg_dump_rx_pkt_info(dhd_pub_t *dhdp, dhd_dbg_rx_info_t *rx_pkt,
	uint16 count)
{
	if (DHD_PKT_MON_DUMP_ON()) {
		DHD_PKT_MON(("\nRX (count: %d)\n", ++count));
		DHD_PKT_MON(("packet fate    = %d\n", rx_pkt->fate));
		__dhd_dbg_dump_pkt_info(dhdp, &rx_pkt->info);
	}
}

int
dhd_dbg_attach_pkt_monitor(dhd_pub_t *dhdp,
		dbg_mon_tx_pkts_t tx_pkt_mon,
		dbg_mon_tx_status_t tx_status_mon,
		dbg_mon_rx_pkts_t rx_pkt_mon)
{

	dhd_dbg_tx_report_t *tx_report = NULL;
	dhd_dbg_rx_report_t *rx_report = NULL;
	dhd_dbg_tx_info_t *tx_pkts = NULL;
	dhd_dbg_rx_info_t *rx_pkts = NULL;
	dhd_dbg_pkt_mon_state_t tx_pkt_state;
	dhd_dbg_pkt_mon_state_t tx_status_state;
	dhd_dbg_pkt_mon_state_t rx_pkt_state;
	gfp_t kflags;
	uint32 alloc_len;
	int ret = BCME_OK;
	unsigned long flags;

	DHD_PKT_INFO(("%s, %d\n", __FUNCTION__, __LINE__));
	if (!dhdp || !dhdp->dbg) {
		DHD_PKT_MON(("%s(): dhdp=%p, dhdp->dbg=%p\n", __FUNCTION__,
			dhdp, (dhdp ? dhdp->dbg : NULL)));
		return -EINVAL;
	}

	DHD_PKT_MON_LOCK(dhdp->dbg->pkt_mon_lock, flags);
	tx_pkt_state = dhdp->dbg->pkt_mon.tx_pkt_state;
	tx_status_state = dhdp->dbg->pkt_mon.tx_pkt_state;
	rx_pkt_state = dhdp->dbg->pkt_mon.rx_pkt_state;

	if (PKT_MON_ATTACHED(tx_pkt_state) || PKT_MON_ATTACHED(tx_status_state) ||
			PKT_MON_ATTACHED(rx_pkt_state)) {
		DHD_PKT_MON(("%s(): packet monitor is already attached, "
			"tx_pkt_state=%d, tx_status_state=%d, rx_pkt_state=%d\n",
			__FUNCTION__, tx_pkt_state, tx_status_state, rx_pkt_state));
		DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
		/* return success as the intention was to initialize packet monitor */
		return BCME_OK;
	}

	kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;

	/* allocate and initialize tx packet monitoring */
	alloc_len = sizeof(*tx_report);
	tx_report = (dhd_dbg_tx_report_t *)kzalloc(alloc_len, kflags);
	if (unlikely(!tx_report)) {
		DHD_ERROR(("%s(): could not allocate memory for - "
			"dhd_dbg_tx_report_t\n", __FUNCTION__));
		ret = -ENOMEM;
		goto fail;
	}

	alloc_len = (sizeof(*tx_pkts) * MAX_FATE_LOG_LEN);
	tx_pkts = (dhd_dbg_tx_info_t *)kzalloc(alloc_len, kflags);
	if (unlikely(!tx_pkts)) {
		DHD_ERROR(("%s(): could not allocate memory for - "
			"dhd_dbg_tx_info_t\n", __FUNCTION__));
		ret = -ENOMEM;
		goto fail;
	}
	dhdp->dbg->pkt_mon.tx_report = tx_report;
	dhdp->dbg->pkt_mon.tx_report->tx_pkts = tx_pkts;
	dhdp->dbg->pkt_mon.tx_pkt_mon = tx_pkt_mon;
	dhdp->dbg->pkt_mon.tx_status_mon = tx_status_mon;
	dhdp->dbg->pkt_mon.tx_pkt_state = PKT_MON_ATTACHED;
	dhdp->dbg->pkt_mon.tx_status_state = PKT_MON_ATTACHED;

	/* allocate and initialze rx packet monitoring */
	alloc_len = sizeof(*rx_report);
	rx_report = (dhd_dbg_rx_report_t *)kzalloc(alloc_len, kflags);
	if (unlikely(!rx_report)) {
		DHD_ERROR(("%s(): could not allocate memory for - "
			"dhd_dbg_rx_report_t\n", __FUNCTION__));
		ret = -ENOMEM;
		goto fail;
	}

	alloc_len = (sizeof(*rx_pkts) * MAX_FATE_LOG_LEN);
	rx_pkts = (dhd_dbg_rx_info_t *)kzalloc(alloc_len, kflags);
	if (unlikely(!rx_pkts)) {
		DHD_ERROR(("%s(): could not allocate memory for - "
			"dhd_dbg_rx_info_t\n", __FUNCTION__));
		ret = -ENOMEM;
		goto fail;
	}
	dhdp->dbg->pkt_mon.rx_report = rx_report;
	dhdp->dbg->pkt_mon.rx_report->rx_pkts = rx_pkts;
	dhdp->dbg->pkt_mon.rx_pkt_mon = rx_pkt_mon;
	dhdp->dbg->pkt_mon.rx_pkt_state = PKT_MON_ATTACHED;

	DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
	DHD_PKT_MON(("%s(): packet monitor attach succeeded\n", __FUNCTION__));
	return ret;

fail:
	/* tx packet monitoring */
	if (tx_pkts) {
		kfree(tx_pkts);
	}
	if (tx_report) {
		kfree(tx_report);
	}
	dhdp->dbg->pkt_mon.tx_report = NULL;
	dhdp->dbg->pkt_mon.tx_report->tx_pkts = NULL;
	dhdp->dbg->pkt_mon.tx_pkt_mon = NULL;
	dhdp->dbg->pkt_mon.tx_status_mon = NULL;
	dhdp->dbg->pkt_mon.tx_pkt_state = PKT_MON_DETACHED;
	dhdp->dbg->pkt_mon.tx_status_state = PKT_MON_DETACHED;

	/* rx packet monitoring */
	if (rx_pkts) {
		kfree(rx_pkts);
	}
	if (rx_report) {
		kfree(rx_report);
	}
	dhdp->dbg->pkt_mon.rx_report = NULL;
	dhdp->dbg->pkt_mon.rx_report->rx_pkts = NULL;
	dhdp->dbg->pkt_mon.rx_pkt_mon = NULL;
	dhdp->dbg->pkt_mon.rx_pkt_state = PKT_MON_DETACHED;

	DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
	DHD_ERROR(("%s(): packet monitor attach failed\n", __FUNCTION__));
	return ret;
}

int
dhd_dbg_start_pkt_monitor(dhd_pub_t *dhdp)
{
	dhd_dbg_tx_report_t *tx_report;
	dhd_dbg_rx_report_t *rx_report;
	dhd_dbg_pkt_mon_state_t tx_pkt_state;
	dhd_dbg_pkt_mon_state_t tx_status_state;
	dhd_dbg_pkt_mon_state_t rx_pkt_state;
	unsigned long flags;

	DHD_PKT_INFO(("%s, %d\n", __FUNCTION__, __LINE__));
	if (!dhdp || !dhdp->dbg) {
		DHD_PKT_MON(("%s(): dhdp=%p, dhdp->dbg=%p\n", __FUNCTION__,
			dhdp, (dhdp ? dhdp->dbg : NULL)));
		return -EINVAL;
	}

	DHD_PKT_MON_LOCK(dhdp->dbg->pkt_mon_lock, flags);
	tx_pkt_state = dhdp->dbg->pkt_mon.tx_pkt_state;
	tx_status_state = dhdp->dbg->pkt_mon.tx_status_state;
	rx_pkt_state = dhdp->dbg->pkt_mon.rx_pkt_state;

	if (PKT_MON_DETACHED(tx_pkt_state) || PKT_MON_DETACHED(tx_status_state) ||
			PKT_MON_DETACHED(rx_pkt_state)) {
		DHD_PKT_MON(("%s(): packet monitor is not yet enabled, "
			"tx_pkt_state=%d, tx_status_state=%d, rx_pkt_state=%d\n",
			__FUNCTION__, tx_pkt_state, tx_status_state, rx_pkt_state));
		DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
		return -EINVAL;
	}

	dhdp->dbg->pkt_mon.tx_pkt_state = PKT_MON_STARTING;
	dhdp->dbg->pkt_mon.tx_status_state = PKT_MON_STARTING;
	dhdp->dbg->pkt_mon.rx_pkt_state = PKT_MON_STARTING;

	tx_report = dhdp->dbg->pkt_mon.tx_report;
	rx_report = dhdp->dbg->pkt_mon.rx_report;
	if (!tx_report || !rx_report) {
		DHD_PKT_MON(("%s(): tx_report=%p, rx_report=%p\n",
			__FUNCTION__, tx_report, rx_report));
		DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
		return -EINVAL;
	}


	tx_pkt_state = dhdp->dbg->pkt_mon.tx_pkt_state;
	tx_status_state = dhdp->dbg->pkt_mon.tx_status_state;
	rx_pkt_state = dhdp->dbg->pkt_mon.rx_pkt_state;

	/* Safe to free packets as state pkt_state is STARTING */
	__dhd_dbg_free_tx_pkts(dhdp, tx_report->tx_pkts, tx_report->pkt_pos);

	__dhd_dbg_free_rx_pkts(dhdp, rx_report->rx_pkts, rx_report->pkt_pos);

	/* reset array postion */
	tx_report->pkt_pos = 0;
	tx_report->status_pos = 0;
	dhdp->dbg->pkt_mon.tx_pkt_state = PKT_MON_STARTED;
	dhdp->dbg->pkt_mon.tx_status_state = PKT_MON_STARTED;

	rx_report->pkt_pos = 0;
	dhdp->dbg->pkt_mon.rx_pkt_state = PKT_MON_STARTED;
	DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);

	DHD_PKT_MON(("%s(): packet monitor started\n", __FUNCTION__));
	return BCME_OK;
}

int
dhd_dbg_monitor_tx_pkts(dhd_pub_t *dhdp, void *pkt, uint32 pktid)
{
	dhd_dbg_tx_report_t *tx_report;
	dhd_dbg_tx_info_t *tx_pkts;
	dhd_dbg_pkt_mon_state_t tx_pkt_state;
	uint32 pkt_hash, driver_ts;
	uint16 pkt_pos;
	unsigned long flags;

	if (!dhdp || !dhdp->dbg) {
		DHD_PKT_MON(("%s(): dhdp=%p, dhdp->dbg=%p\n", __FUNCTION__,
			dhdp, (dhdp ? dhdp->dbg : NULL)));
		return -EINVAL;
	}

	DHD_PKT_MON_LOCK(dhdp->dbg->pkt_mon_lock, flags);
	tx_pkt_state = dhdp->dbg->pkt_mon.tx_pkt_state;
	if (PKT_MON_STARTED(tx_pkt_state)) {
		tx_report = dhdp->dbg->pkt_mon.tx_report;
		pkt_pos = tx_report->pkt_pos;

		if (!PKT_MON_PKT_FULL(pkt_pos)) {
			tx_pkts = tx_report->tx_pkts;
			pkt_hash = __dhd_dbg_pkt_hash((uintptr_t)pkt, pktid);
			driver_ts = __dhd_dbg_driver_ts_usec();

			tx_pkts[pkt_pos].info.pkt = PKTDUP(dhdp->osh, pkt);
			tx_pkts[pkt_pos].info.pkt_len = PKTLEN(dhdp->osh, pkt);
			tx_pkts[pkt_pos].info.pkt_hash = pkt_hash;
			tx_pkts[pkt_pos].info.driver_ts = driver_ts;
			tx_pkts[pkt_pos].info.firmware_ts = 0U;
			tx_pkts[pkt_pos].info.payload_type = FRAME_TYPE_ETHERNET_II;
			tx_pkts[pkt_pos].fate = TX_PKT_FATE_DRV_QUEUED;

			tx_report->pkt_pos++;
		} else {
			dhdp->dbg->pkt_mon.tx_pkt_state = PKT_MON_STOPPED;
			DHD_PKT_MON(("%s(): tx pkt logging stopped, reached "
				"max limit\n", __FUNCTION__));
		}
	}

	DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
	return BCME_OK;
}

int
dhd_dbg_monitor_tx_status(dhd_pub_t *dhdp, void *pkt, uint32 pktid,
		uint16 status)
{
	dhd_dbg_tx_report_t *tx_report;
	dhd_dbg_tx_info_t *tx_pkt;
	dhd_dbg_pkt_mon_state_t tx_status_state;
	wifi_tx_packet_fate pkt_fate;
	uint32 pkt_hash, temp_hash;
	uint16 pkt_pos, status_pos;
	int16 count;
	bool found = FALSE;
	unsigned long flags;

	if (!dhdp || !dhdp->dbg) {
		DHD_PKT_MON(("%s(): dhdp=%p, dhdp->dbg=%p\n", __FUNCTION__,
			dhdp, (dhdp ? dhdp->dbg : NULL)));
		return -EINVAL;
	}

	DHD_PKT_MON_LOCK(dhdp->dbg->pkt_mon_lock, flags);
	tx_status_state = dhdp->dbg->pkt_mon.tx_status_state;
	if (PKT_MON_STARTED(tx_status_state)) {
		tx_report = dhdp->dbg->pkt_mon.tx_report;
		pkt_pos = tx_report->pkt_pos;
		status_pos = tx_report->status_pos;

		if (!PKT_MON_STATUS_FULL(pkt_pos, status_pos)) {
			pkt_hash = __dhd_dbg_pkt_hash((uintptr_t)pkt, pktid);
			pkt_fate = __dhd_dbg_map_tx_status_to_pkt_fate(status);

			/* best bet (in-order tx completion) */
			count = status_pos;
			tx_pkt = (((dhd_dbg_tx_info_t *)tx_report->tx_pkts) + status_pos);
			while ((count < pkt_pos) && tx_pkt) {
				temp_hash = tx_pkt->info.pkt_hash;
				if (temp_hash == pkt_hash) {
					tx_pkt->fate = pkt_fate;
					tx_report->status_pos++;
					found = TRUE;
					break;
				}
				tx_pkt++;
				count++;
			}

			/* search until beginning (handles out-of-order completion) */
			if (!found) {
				count = status_pos - 1;
				tx_pkt = (((dhd_dbg_tx_info_t *)tx_report->tx_pkts) + count);
				while ((count >= 0) && tx_pkt) {
					temp_hash = tx_pkt->info.pkt_hash;
					if (temp_hash == pkt_hash) {
						tx_pkt->fate = pkt_fate;
						tx_report->status_pos++;
						found = TRUE;
						break;
					}
					tx_pkt--;
					count--;
				}

				if (!found) {
					/* still couldn't match tx_status */
					DHD_ERROR(("%s(): couldn't match tx_status, pkt_pos=%u, "
						"status_pos=%u, pkt_fate=%u\n", __FUNCTION__,
						pkt_pos, status_pos, pkt_fate));
				}
			}
		} else {
			dhdp->dbg->pkt_mon.tx_status_state = PKT_MON_STOPPED;
			DHD_PKT_MON(("%s(): tx_status logging stopped, reached "
				"max limit\n", __FUNCTION__));
		}
	}

	DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
	return BCME_OK;
}

int
dhd_dbg_monitor_rx_pkts(dhd_pub_t *dhdp, void *pkt)
{
	dhd_dbg_rx_report_t *rx_report;
	dhd_dbg_rx_info_t *rx_pkts;
	dhd_dbg_pkt_mon_state_t rx_pkt_state;
	uint32 driver_ts;
	uint16 pkt_pos;
	unsigned long flags;

	if (!dhdp || !dhdp->dbg) {
		DHD_PKT_MON(("%s(): dhdp=%p, dhdp->dbg=%p\n", __FUNCTION__,
			dhdp, (dhdp ? dhdp->dbg : NULL)));
		return -EINVAL;
	}

	DHD_PKT_MON_LOCK(dhdp->dbg->pkt_mon_lock, flags);
	rx_pkt_state = dhdp->dbg->pkt_mon.rx_pkt_state;
	if (PKT_MON_STARTED(rx_pkt_state)) {
		rx_report = dhdp->dbg->pkt_mon.rx_report;
		pkt_pos = rx_report->pkt_pos;

		if (!PKT_MON_PKT_FULL(pkt_pos)) {
			rx_pkts = rx_report->rx_pkts;
			driver_ts = __dhd_dbg_driver_ts_usec();

			rx_pkts[pkt_pos].info.pkt = PKTDUP(dhdp->osh, pkt);
			rx_pkts[pkt_pos].info.pkt_len = PKTLEN(dhdp->osh, pkt);
			rx_pkts[pkt_pos].info.pkt_hash = 0U;
			rx_pkts[pkt_pos].info.driver_ts = driver_ts;
			rx_pkts[pkt_pos].info.firmware_ts = 0U;
			rx_pkts[pkt_pos].info.payload_type = FRAME_TYPE_ETHERNET_II;
			rx_pkts[pkt_pos].fate = RX_PKT_FATE_SUCCESS;

			rx_report->pkt_pos++;
		} else {
			dhdp->dbg->pkt_mon.rx_pkt_state = PKT_MON_STOPPED;
			DHD_PKT_MON(("%s(): rx pkt logging stopped, reached "
					"max limit\n", __FUNCTION__));
		}
	}

	DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
	return BCME_OK;
}

int
dhd_dbg_stop_pkt_monitor(dhd_pub_t *dhdp)
{
	dhd_dbg_pkt_mon_state_t tx_pkt_state;
	dhd_dbg_pkt_mon_state_t tx_status_state;
	dhd_dbg_pkt_mon_state_t rx_pkt_state;
	unsigned long flags;

	DHD_PKT_INFO(("%s, %d\n", __FUNCTION__, __LINE__));
	if (!dhdp || !dhdp->dbg) {
		DHD_PKT_MON(("%s(): dhdp=%p, dhdp->dbg=%p\n", __FUNCTION__,
			dhdp, (dhdp ? dhdp->dbg : NULL)));
		return -EINVAL;
	}

	DHD_PKT_MON_LOCK(dhdp->dbg->pkt_mon_lock, flags);
	tx_pkt_state = dhdp->dbg->pkt_mon.tx_pkt_state;
	tx_status_state = dhdp->dbg->pkt_mon.tx_status_state;
	rx_pkt_state = dhdp->dbg->pkt_mon.rx_pkt_state;

	if (PKT_MON_DETACHED(tx_pkt_state) || PKT_MON_DETACHED(tx_status_state) ||
			PKT_MON_DETACHED(rx_pkt_state)) {
		DHD_PKT_MON(("%s(): packet monitor is not yet enabled, "
			"tx_pkt_state=%d, tx_status_state=%d, rx_pkt_state=%d\n",
			__FUNCTION__, tx_pkt_state, tx_status_state, rx_pkt_state));
		DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
		return -EINVAL;
	}
	dhdp->dbg->pkt_mon.tx_pkt_state = PKT_MON_STOPPED;
	dhdp->dbg->pkt_mon.tx_status_state = PKT_MON_STOPPED;
	dhdp->dbg->pkt_mon.rx_pkt_state = PKT_MON_STOPPED;
	DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);

	DHD_PKT_MON(("%s(): packet monitor stopped\n", __FUNCTION__));
	return BCME_OK;
}

#define __COPY_TO_USER(to, from, n) \
	do { \
		int __ret; \
		__ret = copy_to_user((void __user *)(to), (void *)(from), \
				(unsigned long)(n)); \
		if (unlikely(__ret)) { \
			DHD_ERROR(("%s():%d: copy_to_user failed, ret=%d\n", \
				__FUNCTION__, __LINE__, __ret)); \
			return __ret; \
		} \
	} while (0);

int
dhd_dbg_monitor_get_tx_pkts(dhd_pub_t *dhdp, void __user *user_buf,
		uint16 req_count, uint16 *resp_count)
{
	dhd_dbg_tx_report_t *tx_report;
	dhd_dbg_tx_info_t *tx_pkt;
	wifi_tx_report_t *ptr;
	compat_wifi_tx_report_t *cptr;
	dhd_dbg_pkt_mon_state_t tx_pkt_state;
	dhd_dbg_pkt_mon_state_t tx_status_state;
	uint16 pkt_count, count;
	unsigned long flags;

	DHD_PKT_INFO(("%s, %d\n", __FUNCTION__, __LINE__));
	BCM_REFERENCE(ptr);
	BCM_REFERENCE(cptr);

	if (!dhdp || !dhdp->dbg) {
		DHD_PKT_MON(("%s(): dhdp=%p, dhdp->dbg=%p\n", __FUNCTION__,
			dhdp, (dhdp ? dhdp->dbg : NULL)));
		return -EINVAL;
	}

	DHD_PKT_MON_LOCK(dhdp->dbg->pkt_mon_lock, flags);
	tx_pkt_state = dhdp->dbg->pkt_mon.tx_pkt_state;
	tx_status_state = dhdp->dbg->pkt_mon.tx_status_state;
	if (PKT_MON_DETACHED(tx_pkt_state) || PKT_MON_DETACHED(tx_status_state)) {
		DHD_PKT_MON(("%s(): packet monitor is not yet enabled, "
			"tx_pkt_state=%d, tx_status_state=%d\n", __FUNCTION__,
			tx_pkt_state, tx_status_state));
		DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
		return -EINVAL;
	}

	count = 0;
	tx_report = dhdp->dbg->pkt_mon.tx_report;
	tx_pkt = tx_report->tx_pkts;
	pkt_count = MIN(req_count, tx_report->status_pos);

#ifdef CONFIG_COMPAT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0))
	if (in_compat_syscall())
#else
	if (is_compat_task())
#endif
	{
		cptr = (compat_wifi_tx_report_t *)user_buf;
		while ((count < pkt_count) && tx_pkt && cptr) {
			compat_wifi_tx_report_t *comp_ptr = compat_ptr((uintptr_t) cptr);
			compat_dhd_dbg_pkt_info_t compat_tx_pkt;
			__dhd_dbg_dump_tx_pkt_info(dhdp, tx_pkt, count);
			__COPY_TO_USER(&comp_ptr->fate, &tx_pkt->fate, sizeof(tx_pkt->fate));

			compat_tx_pkt.payload_type = tx_pkt->info.payload_type;
			compat_tx_pkt.pkt_len = tx_pkt->info.pkt_len;
			compat_tx_pkt.driver_ts = tx_pkt->info.driver_ts;
			compat_tx_pkt.firmware_ts = tx_pkt->info.firmware_ts;
			compat_tx_pkt.pkt_hash = tx_pkt->info.pkt_hash;
			__COPY_TO_USER(&comp_ptr->frame_inf.payload_type,
				&compat_tx_pkt.payload_type,
				OFFSETOF(compat_dhd_dbg_pkt_info_t, pkt_hash));
			__COPY_TO_USER(comp_ptr->frame_inf.frame_content.ethernet_ii,
				PKTDATA(dhdp->osh, tx_pkt->info.pkt), tx_pkt->info.pkt_len);

			cptr++;
			tx_pkt++;
			count++;
		}
	} else
#endif /* CONFIG_COMPAT */

	{
		ptr = (wifi_tx_report_t *)user_buf;
		while ((count < pkt_count) && tx_pkt && ptr) {
			__dhd_dbg_dump_tx_pkt_info(dhdp, tx_pkt, count);
			__COPY_TO_USER(&ptr->fate, &tx_pkt->fate, sizeof(tx_pkt->fate));
			__COPY_TO_USER(&ptr->frame_inf.payload_type,
				&tx_pkt->info.payload_type,
				OFFSETOF(dhd_dbg_pkt_info_t, pkt_hash));
			__COPY_TO_USER(ptr->frame_inf.frame_content.ethernet_ii,
				PKTDATA(dhdp->osh, tx_pkt->info.pkt), tx_pkt->info.pkt_len);

			ptr++;
			tx_pkt++;
			count++;
		}
	}
	*resp_count = pkt_count;

	DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
	if (!pkt_count) {
		DHD_ERROR(("%s(): no tx_status in tx completion messages, "
			"make sure that 'd11status' is enabled in firmware, "
			"status_pos=%u\n", __FUNCTION__, pkt_count));
	}

	return BCME_OK;
}

int
dhd_dbg_monitor_get_rx_pkts(dhd_pub_t *dhdp, void __user *user_buf,
		uint16 req_count, uint16 *resp_count)
{
	dhd_dbg_rx_report_t *rx_report;
	dhd_dbg_rx_info_t *rx_pkt;
	wifi_rx_report_t *ptr;
	compat_wifi_rx_report_t *cptr;
	dhd_dbg_pkt_mon_state_t rx_pkt_state;
	uint16 pkt_count, count;
	unsigned long flags;

	DHD_PKT_INFO(("%s, %d\n", __FUNCTION__, __LINE__));
	BCM_REFERENCE(ptr);
	BCM_REFERENCE(cptr);

	if (!dhdp || !dhdp->dbg) {
		DHD_PKT_MON(("%s(): dhdp=%p, dhdp->dbg=%p\n", __FUNCTION__,
			dhdp, (dhdp ? dhdp->dbg : NULL)));
		return -EINVAL;
	}

	DHD_PKT_MON_LOCK(dhdp->dbg->pkt_mon_lock, flags);
	rx_pkt_state = dhdp->dbg->pkt_mon.rx_pkt_state;
	if (PKT_MON_DETACHED(rx_pkt_state)) {
		DHD_PKT_MON(("%s(): packet fetch is not allowed , "
			"rx_pkt_state=%d\n", __FUNCTION__, rx_pkt_state));
		DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
		return -EINVAL;
	}

	count = 0;
	rx_report = dhdp->dbg->pkt_mon.rx_report;
	rx_pkt = rx_report->rx_pkts;
	pkt_count = MIN(req_count, rx_report->pkt_pos);

#ifdef CONFIG_COMPAT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0))
	if (in_compat_syscall())
#else
	if (is_compat_task())
#endif
	{
		cptr = (compat_wifi_rx_report_t *)user_buf;
		while ((count < pkt_count) && rx_pkt && cptr) {
			compat_wifi_rx_report_t *comp_ptr = compat_ptr((uintptr_t) cptr);
			compat_dhd_dbg_pkt_info_t compat_rx_pkt;
			__dhd_dbg_dump_rx_pkt_info(dhdp, rx_pkt, count);
			__COPY_TO_USER(&comp_ptr->fate, &rx_pkt->fate, sizeof(rx_pkt->fate));

			compat_rx_pkt.payload_type = rx_pkt->info.payload_type;
			compat_rx_pkt.pkt_len = rx_pkt->info.pkt_len;
			compat_rx_pkt.driver_ts = rx_pkt->info.driver_ts;
			compat_rx_pkt.firmware_ts = rx_pkt->info.firmware_ts;
			compat_rx_pkt.pkt_hash = rx_pkt->info.pkt_hash;
			__COPY_TO_USER(&comp_ptr->frame_inf.payload_type,
				&compat_rx_pkt.payload_type,
				OFFSETOF(compat_dhd_dbg_pkt_info_t, pkt_hash));
			__COPY_TO_USER(comp_ptr->frame_inf.frame_content.ethernet_ii,
				PKTDATA(dhdp->osh, rx_pkt->info.pkt), rx_pkt->info.pkt_len);

			cptr++;
			rx_pkt++;
			count++;
		}
	} else
#endif /* CONFIG_COMPAT */
	{
		ptr = (wifi_rx_report_t *)user_buf;
		while ((count < pkt_count) && rx_pkt && ptr) {
			__dhd_dbg_dump_rx_pkt_info(dhdp, rx_pkt, count);

			__COPY_TO_USER(&ptr->fate, &rx_pkt->fate, sizeof(rx_pkt->fate));
			__COPY_TO_USER(&ptr->frame_inf.payload_type,
				&rx_pkt->info.payload_type,
				OFFSETOF(dhd_dbg_pkt_info_t, pkt_hash));
			__COPY_TO_USER(ptr->frame_inf.frame_content.ethernet_ii,
				PKTDATA(dhdp->osh, rx_pkt->info.pkt), rx_pkt->info.pkt_len);

			ptr++;
			rx_pkt++;
			count++;
		}
	}

	*resp_count = pkt_count;
	DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);

	return BCME_OK;
}

int
dhd_dbg_detach_pkt_monitor(dhd_pub_t *dhdp)
{
	dhd_dbg_tx_report_t *tx_report;
	dhd_dbg_rx_report_t *rx_report;
	dhd_dbg_pkt_mon_state_t tx_pkt_state;
	dhd_dbg_pkt_mon_state_t tx_status_state;
	dhd_dbg_pkt_mon_state_t rx_pkt_state;
	unsigned long flags;

	DHD_PKT_INFO(("%s, %d\n", __FUNCTION__, __LINE__));
	if (!dhdp || !dhdp->dbg) {
		DHD_PKT_MON(("%s(): dhdp=%p, dhdp->dbg=%p\n", __FUNCTION__,
			dhdp, (dhdp ? dhdp->dbg : NULL)));
		return -EINVAL;
	}

	DHD_PKT_MON_LOCK(dhdp->dbg->pkt_mon_lock, flags);
	tx_pkt_state = dhdp->dbg->pkt_mon.tx_pkt_state;
	tx_status_state = dhdp->dbg->pkt_mon.tx_status_state;
	rx_pkt_state = dhdp->dbg->pkt_mon.rx_pkt_state;

	if (PKT_MON_DETACHED(tx_pkt_state) || PKT_MON_DETACHED(tx_status_state) ||
			PKT_MON_DETACHED(rx_pkt_state)) {
		DHD_PKT_MON(("%s(): packet monitor is already detached, "
			"tx_pkt_state=%d, tx_status_state=%d, rx_pkt_state=%d\n",
			__FUNCTION__, tx_pkt_state, tx_status_state, rx_pkt_state));
		DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
		return -EINVAL;
	}

	tx_report = dhdp->dbg->pkt_mon.tx_report;
	rx_report = dhdp->dbg->pkt_mon.rx_report;

	/* free and de-initalize tx packet monitoring */
	dhdp->dbg->pkt_mon.tx_pkt_state = PKT_MON_DETACHED;
	dhdp->dbg->pkt_mon.tx_status_state = PKT_MON_DETACHED;
	if (tx_report) {
		if (tx_report->tx_pkts) {
			__dhd_dbg_free_tx_pkts(dhdp, tx_report->tx_pkts,
				tx_report->pkt_pos);
			kfree(tx_report->tx_pkts);
			dhdp->dbg->pkt_mon.tx_report->tx_pkts = NULL;
		}
		kfree(tx_report);
		dhdp->dbg->pkt_mon.tx_report = NULL;
	}
	dhdp->dbg->pkt_mon.tx_pkt_mon = NULL;
	dhdp->dbg->pkt_mon.tx_status_mon = NULL;

	/* free and de-initalize rx packet monitoring */
	dhdp->dbg->pkt_mon.rx_pkt_state = PKT_MON_DETACHED;
	if (rx_report) {
		if (rx_report->rx_pkts) {
			__dhd_dbg_free_rx_pkts(dhdp, rx_report->rx_pkts,
				rx_report->pkt_pos);
			kfree(rx_report->rx_pkts);
			dhdp->dbg->pkt_mon.rx_report->rx_pkts = NULL;
		}
		kfree(rx_report);
		dhdp->dbg->pkt_mon.rx_report = NULL;
	}
	dhdp->dbg->pkt_mon.rx_pkt_mon = NULL;

	DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
	DHD_PKT_MON(("%s(): packet monitor detach succeeded\n", __FUNCTION__));
	return BCME_OK;
}
#endif /* DBG_PKT_MON */

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
			(uint8 *)FW_VERBOSE_RING_NAME, FW_VERBOSE_RING_SIZE, DHD_PREALLOC_FW_VERBOSE_RING);
	if (ret)
		goto error;

	ret = dhd_dbg_ring_init(dhdp, &dbg->dbg_rings[FW_EVENT_RING_ID], FW_EVENT_RING_ID,
			(uint8 *)FW_EVENT_RING_NAME, FW_EVENT_RING_SIZE, DHD_PREALLOC_FW_EVENT_RING);
	if (ret)
		goto error;

	ret = dhd_dbg_ring_init(dhdp, &dbg->dbg_rings[DHD_EVENT_RING_ID], DHD_EVENT_RING_ID,
			(uint8 *)DHD_EVENT_RING_NAME, DHD_EVENT_RING_SIZE, DHD_PREALLOC_DHD_EVENT_RING);
	if (ret)
		goto error;

	ret = dhd_dbg_ring_init(dhdp, &dbg->dbg_rings[NAN_EVENT_RING_ID], NAN_EVENT_RING_ID,
			(uint8 *)NAN_EVENT_RING_NAME, NAN_EVENT_RING_SIZE, DHD_PREALLOC_NAN_EVENT_RING);
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
