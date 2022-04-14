/*
 * DHD debugability support
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#include <typedefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <dhd_dbg_ring.h>
#include <dhd_debug.h>
#include <dhd_mschdbg.h>
#include <dhd_bus.h>

#include <event_log.h>
#include <event_trace.h>
#include <msgtrace.h>

#if defined(DHD_EVENT_LOG_FILTER)
#include <dhd_event_log_filter.h>
#endif /* DHD_EVENT_LOG_FILTER */

#if defined(DHD_EFI) || defined(NDIS)
#if !defined(offsetof)
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif	/* !defined(offsetof) */

#define container_of(ptr, type, member) \
		(type *)((char *)(ptr) - offsetof(type, member))
#endif /* defined(DHD_EFI ) || defined(NDIS) */

#ifdef DHD_DEBUGABILITY_LOG_DUMP_RING
uint8 control_logtrace = LOGTRACE_RAW_FMT;
#else
uint8 control_logtrace = CUSTOM_CONTROL_LOGTRACE;
#endif

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
#ifndef DISABLE_PCI_LOGGING
	{1, EVENT_LOG_TAG_PCI_WARN, "PCI_WARN"},
	{2, EVENT_LOG_TAG_PCI_INFO, "PCI_INFO"},
	{3, EVENT_LOG_TAG_PCI_DBG, "PCI_DEBUG"},
#endif
#ifndef DISABLE_BEACON_LOGGING
	{3, EVENT_LOG_TAG_BEACON_LOG, "BEACON_LOG"},
#endif
	{2, EVENT_LOG_TAG_WL_ASSOC_LOG, "ASSOC_LOG"},
	{2, EVENT_LOG_TAG_WL_ROAM_LOG, "ROAM_LOG"},
	{1, EVENT_LOG_TAG_TRACE_WL_INFO, "WL INFO"},
	{1, EVENT_LOG_TAG_TRACE_BTCOEX_INFO, "BTCOEX INFO"},
#ifdef DHD_RANDMAC_LOGGING
	{1, EVENT_LOG_TAG_RANDMAC_ERR, "RANDMAC_ERR"},
#endif /* DHD_RANDMAC_LOGGING */
#ifdef CUSTOMER_HW4_DEBUG
	{3, EVENT_LOG_TAG_SCAN_WARN, "SCAN_WARN"},
#else
	{1, EVENT_LOG_TAG_SCAN_WARN, "SCAN_WARN"},
#endif /* CUSTOMER_HW4_DEBUG */
	{1, EVENT_LOG_TAG_SCAN_ERROR, "SCAN_ERROR"},
	{2, EVENT_LOG_TAG_SCAN_TRACE_LOW, "SCAN_TRACE_LOW"},
	{2, EVENT_LOG_TAG_SCAN_TRACE_HIGH, "SCAN_TRACE_HIGH"},
#ifdef DHD_WL_ERROR_LOGGING
	{3, EVENT_LOG_TAG_WL_ERROR, "WL_ERROR"},
#endif
#ifdef DHD_IE_ERROR_LOGGING
	{3, EVENT_LOG_TAG_IE_ERROR, "IE_ERROR"},
#endif
#ifdef DHD_ASSOC_ERROR_LOGGING
	{3, EVENT_LOG_TAG_ASSOC_ERROR, "ASSOC_ERROR"},
#endif
#ifdef DHD_PMU_ERROR_LOGGING
	{3, EVENT_LOG_TAG_PMU_ERROR, "PMU_ERROR"},
#endif
#ifdef DHD_8021X_ERROR_LOGGING
	{3, EVENT_LOG_TAG_4WAYHANDSHAKE, "8021X_ERROR"},
#endif
#ifdef DHD_AMPDU_ERROR_LOGGING
	{3, EVENT_LOG_TAG_AMSDU_ERROR, "AMPDU_ERROR"},
#endif
#ifdef DHD_SAE_ERROR_LOGGING
	{3, EVENT_LOG_TAG_SAE_ERROR, "SAE_ERROR"},
#endif
};

/* reference tab table */
uint ref_tag_tbl[EVENT_LOG_TAG_MAX + 1] = {0};

typedef struct dhddbg_loglist_item {
	dll_t list;
	prcd_event_log_hdr_t prcd_log_hdr;
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
dhd_dbg_push_to_ring(dhd_pub_t *dhdp, int ring_id, dhd_dbg_ring_entry_t *hdr, void *data)
{
	dhd_dbg_ring_t *ring;
	int ret = 0;
	uint32 pending_len = 0;

	if (!dhdp || !dhdp->dbg) {
		return BCME_BADADDR;
	}

	if (!VALID_RING(ring_id)) {
		DHD_ERROR(("%s : invalid ring_id : %d\n", __FUNCTION__, ring_id));
		return BCME_RANGE;
	}

	ring = &dhdp->dbg->dbg_rings[ring_id];

	ret = dhd_dbg_ring_push(ring, hdr, data);
	if (ret != BCME_OK)
		return ret;

	pending_len = dhd_dbg_ring_get_pending_len(ring);
	dhd_dbg_ring_sched_pull(ring, pending_len, dhdp->dbg->pullreq,
			dhdp->dbg->private, ring->id);

	return ret;
}

dhd_dbg_ring_t *
dhd_dbg_get_ring_from_ring_id(dhd_pub_t *dhdp, int ring_id)
{
	if (!dhdp || !dhdp->dbg) {
		return NULL;
	}

	if (!VALID_RING(ring_id)) {
		DHD_ERROR(("%s : invalid ring_id : %d\n", __FUNCTION__, ring_id));
		return NULL;
	}

	return &dhdp->dbg->dbg_rings[ring_id];
}

int
dhd_dbg_pull_single_from_ring(dhd_pub_t *dhdp, int ring_id, void *data, uint32 buf_len,
	bool strip_header)
{
	dhd_dbg_ring_t *ring;

	if (!dhdp || !dhdp->dbg) {
		return 0;
	}

	if (!VALID_RING(ring_id)) {
		DHD_ERROR(("%s : invalid ring_id : %d\n", __FUNCTION__, ring_id));
		return BCME_RANGE;
	}

	ring = &dhdp->dbg->dbg_rings[ring_id];

	return dhd_dbg_ring_pull_single(ring, data, buf_len, strip_header);
}

int
dhd_dbg_pull_from_ring(dhd_pub_t *dhdp, int ring_id, void *data, uint32 buf_len)
{
	dhd_dbg_ring_t *ring;

	if (!dhdp || !dhdp->dbg)
		return 0;
	if (!VALID_RING(ring_id)) {
		DHD_ERROR(("%s : invalid ring_id : %d\n", __FUNCTION__, ring_id));
		return BCME_RANGE;
	}
	ring = &dhdp->dbg->dbg_rings[ring_id];
	return dhd_dbg_ring_pull(ring, data, buf_len, FALSE);
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

	if (!event_data) {
		DHD_ERROR(("%s: event_data is NULL\n", __FUNCTION__));
		return;
	}

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
#define DATA_UNIT_FOR_LOG_CNT 4

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

int
replace_percent_p_to_x(char *fmt)
{
	int p_to_x_done = FALSE;

	while (*fmt != '\0')
	{
		/* Skip characters will we see a % */
		if (*fmt++ != '%')
		{
			continue;
		}

		/*
		 * Skip any flags, field width and precision:
		 *Flags: Followed by %
		 * #, 0, -, ' ', +
		 */
		if (*fmt == '#')
			fmt++;

		if (*fmt == '0' || *fmt == '-' || *fmt == '+')
			fmt++;

		/*
		 * Field width:
		 * An optional decimal digit string (with non-zero first digit)
		 * specifying a minimum field width
		 */
		while (*fmt && bcm_isdigit(*fmt))
			fmt++;

		/*
		 * Precision:
		 * An optional precision, in the form of a period ('.')  followed by an
		 * optional decimal digit string.
		 */
		if (*fmt == '.')
		{
			fmt++;
			while (*fmt && bcm_isdigit(*fmt)) fmt++;
		}

		/* If %p is seen, change it to %x */
		if (*fmt == 'p')
		{
			*fmt = 'x';
			p_to_x_done = TRUE;
		}
		if (*fmt)
			fmt++;
	}

	return p_to_x_done;
}

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

/* To identify format of non string format types */
bool
check_valid_non_string_format(char *curr_ptr)
{
	char *next_ptr;
	char *next_fmt_stptr;
	char valid_fmt_types[17] = {'d', 'i', 'x', 'X', 'c', 'p', 'u',
			'f', 'F', 'e', 'E', 'g', 'G', 'o',
			'a', 'A', 'n'};
	int i;
	bool valid = FALSE;

	/* Check for next % in the fmt str */
	next_fmt_stptr = bcmstrstr(curr_ptr, "%");

	for (next_ptr = curr_ptr; *next_ptr != '\0'; next_ptr++) {
		for (i = 0; i < (int)((sizeof(valid_fmt_types))/sizeof(valid_fmt_types[0])); i++) {
			if (*next_ptr == valid_fmt_types[i]) {
				/* Check whether format type found corresponds to current %
				 * and not the next one, if exists.
				 */
				if ((next_fmt_stptr == NULL) ||
						(next_fmt_stptr && (next_ptr < next_fmt_stptr))) {
					/* Not validating for length/width fields in
					 * format specifier.
					 */
					valid = TRUE;
				}
				goto done;
			}
		}
	}

done:
	return valid;
}

#define MAX_NO_OF_ARG	16
#define FMTSTR_SIZE	200
#define ROMSTR_SIZE	268
#define SIZE_LOC_STR	50
#define LOG_PRINT_CNT_MAX	16u
#define EL_MSEC_PER_SEC	1000
#ifdef DHD_LOG_PRINT_RATE_LIMIT
#define MAX_LOG_PRINT_COUNT 100u
#define LOG_PRINT_THRESH (1u * USEC_PER_SEC)
#endif
#define EL_PARSE_VER	"V02"
static uint64 verboselog_ts_saved = 0;

bool
dhd_dbg_process_event_log_hdr(event_log_hdr_t *log_hdr, prcd_event_log_hdr_t *prcd_log_hdr)
{
	event_log_extended_hdr_t *ext_log_hdr;
	uint16 event_log_fmt_num;
	uint8 event_log_hdr_type;

	/* Identify the type of event tag, payload type etc..  */
	event_log_hdr_type = log_hdr->fmt_num & DHD_EVENT_LOG_HDR_MASK;
	event_log_fmt_num = (log_hdr->fmt_num >> DHD_EVENT_LOG_FMT_NUM_OFFSET) &
		DHD_EVENT_LOG_FMT_NUM_MASK;

	switch (event_log_hdr_type) {
		case DHD_OW_NB_EVENT_LOG_HDR:
			prcd_log_hdr->ext_event_log_hdr = FALSE;
			prcd_log_hdr->binary_payload = FALSE;
			break;
		case DHD_TW_NB_EVENT_LOG_HDR:
			prcd_log_hdr->ext_event_log_hdr = TRUE;
			prcd_log_hdr->binary_payload = FALSE;
			break;
		case DHD_BI_EVENT_LOG_HDR:
			if (event_log_fmt_num == DHD_OW_BI_EVENT_FMT_NUM) {
				prcd_log_hdr->ext_event_log_hdr = FALSE;
				prcd_log_hdr->binary_payload = TRUE;
			} else if (event_log_fmt_num == DHD_TW_BI_EVENT_FMT_NUM) {
				prcd_log_hdr->ext_event_log_hdr = TRUE;
				prcd_log_hdr->binary_payload = TRUE;
			} else {
				DHD_ERROR(("%s: invalid format number 0x%X\n",
					__FUNCTION__, event_log_fmt_num));
				return FALSE;
			}
			break;
		case DHD_INVALID_EVENT_LOG_HDR:
		default:
			DHD_ERROR(("%s: invalid event log header type 0x%X\n",
				__FUNCTION__, event_log_hdr_type));
			return FALSE;
	}

	/* Parse extended and legacy event log headers and populate prcd_event_log_hdr_t */
	if (prcd_log_hdr->ext_event_log_hdr) {
		ext_log_hdr = (event_log_extended_hdr_t *)
			((uint8 *)log_hdr - sizeof(event_log_hdr_t));
		prcd_log_hdr->tag = ((ext_log_hdr->extended_tag &
			DHD_TW_VALID_TAG_BITS_MASK) << DHD_TW_EVENT_LOG_TAG_OFFSET) | log_hdr->tag;
	} else {
		prcd_log_hdr->tag = log_hdr->tag;
	}
	prcd_log_hdr->count = log_hdr->count;
	prcd_log_hdr->fmt_num_raw = log_hdr->fmt_num;
	prcd_log_hdr->fmt_num = event_log_fmt_num;

	/* update arm cycle */
	/*
	 * For loegacy event tag :-
	 * |payload........|Timestamp| Tag
	 *
	 * For extended event tag:-
	 * |payload........|Timestamp|extended Tag| Tag.
	 *
	 */
	prcd_log_hdr->armcycle = prcd_log_hdr->ext_event_log_hdr ?
		*(uint32 *)(log_hdr - EVENT_TAG_TIMESTAMP_EXT_OFFSET) :
		*(uint32 *)(log_hdr - EVENT_TAG_TIMESTAMP_OFFSET);

	/* update event log data pointer address */
	prcd_log_hdr->log_ptr =
		(uint32 *)log_hdr - log_hdr->count - prcd_log_hdr->ext_event_log_hdr;

	/* handle error cases above this */
	return TRUE;
}

static void
dhd_dbg_verboselog_handler(dhd_pub_t *dhdp, prcd_event_log_hdr_t *plog_hdr,
		void *raw_event_ptr, uint32 logset, uint16 block, uint32* data)
{
	event_log_hdr_t *ts_hdr;
	uint32 *log_ptr = plog_hdr->log_ptr;
	char fmtstr_loc_buf[ROMSTR_SIZE] = { 0 };
	uint32 rom_str_len = 0;
	uint32 *ts_data;

	if (!raw_event_ptr) {
		return;
	}

	if (log_ptr < data) {
		DHD_ERROR(("Invalid log pointer, logptr : %p data : %p \n", log_ptr, data));
		return;
	}

	if (log_ptr > data) {
		/* Get time stamp if it's updated */
		ts_hdr = (event_log_hdr_t *)((char *)log_ptr - sizeof(event_log_hdr_t));
		if (ts_hdr->tag == EVENT_LOG_TAG_TS) {
			ts_data = (uint32 *)ts_hdr - ts_hdr->count;
			if (ts_data >= data) {
				verboselog_ts_saved = (uint64)ts_data[0];
				DHD_MSGTRACE_LOG(("EVENT_LOG_TS[0x%08x]: SYS:%08x CPU:%08x\n",
					ts_data[ts_hdr->count - 1], ts_data[0], ts_data[1]));
			}
		} else if (ts_hdr->tag == EVENT_LOG_TAG_ENHANCED_TS) {
			ets_msg_v1_t *ets;
			ets = (ets_msg_v1_t *)ts_hdr - ts_hdr->count;
			if ((uint32*)ets >= data &&
				ts_hdr->count >= (sizeof(ets_msg_v1_t) / sizeof(uint32)) &&
				ets->version == ENHANCED_TS_MSG_VERSION_1) {
				DHD_MSGTRACE_LOG(("EVENT_LOG_ENHANCED_TS_V1: "
					"SYS:%08x CPU:%08x CPUFREQ:%u\n",
					ets->timestamp, ets->cyclecount, ets->cpu_freq));
			}
		}
	}

	if (plog_hdr->tag == EVENT_LOG_TAG_ROM_PRINTF) {
		rom_str_len = (plog_hdr->count - 1) * sizeof(uint32);
		if (rom_str_len >= (ROMSTR_SIZE -1))
			rom_str_len = ROMSTR_SIZE - 1;

		/* copy all ascii data for ROM printf to local string */
		memcpy(fmtstr_loc_buf, log_ptr, rom_str_len);
		/* add end of line at last */
		fmtstr_loc_buf[rom_str_len] = '\0';

		DHD_MSGTRACE_LOG(("EVENT_LOG_ROM[0x%08x]: %s",
				log_ptr[plog_hdr->count - 1], fmtstr_loc_buf));

		/* Add newline if missing */
		if (fmtstr_loc_buf[strlen(fmtstr_loc_buf) - 1] != '\n')
			DHD_MSGTRACE_LOG(("\n"));

		return;
	}

	if (plog_hdr->tag == EVENT_LOG_TAG_MSCHPROFILE ||
		plog_hdr->tag == EVENT_LOG_TAG_MSCHPROFILE_TLV) {
		wl_mschdbg_verboselog_handler(dhdp, raw_event_ptr, plog_hdr, log_ptr);
		return;
	}

	/* print the message out in a logprint  */
	dhd_dbg_verboselog_printf(dhdp, plog_hdr, raw_event_ptr, log_ptr, logset, block);
}

void
dhd_dbg_verboselog_printf(dhd_pub_t *dhdp, prcd_event_log_hdr_t *plog_hdr,
	void *raw_event_ptr, uint32 *log_ptr, uint32 logset, uint16 block)
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
	struct bcmstrbuf b;
#ifdef DHD_LOG_PRINT_RATE_LIMIT
	static int log_print_count = 0;
	static uint64 ts0 = 0;
	uint64 ts1 = 0;
#endif /* DHD_LOG_PRINT_RATE_LIMIT */

	BCM_REFERENCE(arg);

#ifdef DHD_LOG_PRINT_RATE_LIMIT
	if (!ts0)
		ts0 = OSL_SYSUPTIME_US();

	ts1 = OSL_SYSUPTIME_US();

	if (((ts1 - ts0) <= LOG_PRINT_THRESH) && (log_print_count >= MAX_LOG_PRINT_COUNT)) {
		log_print_threshold = 1;
		ts0 = 0;
		log_print_count = 0;
		DHD_ERROR(("%s: Log print water mark is reached,"
			" console logs are dumped only to debug_dump file\n", __FUNCTION__));
	} else if ((ts1 - ts0) > LOG_PRINT_THRESH) {
		log_print_threshold = 0;
		ts0 = 0;
		log_print_count = 0;
	}

#endif /* DHD_LOG_PRINT_RATE_LIMIT */
	/* print the message out in a logprint  */
	if ((control_logtrace == LOGTRACE_RAW_FMT) || !(raw_event->fmts)) {
		if (dhdp->dbg) {
			log_level = dhdp->dbg->dbg_rings[FW_VERBOSE_RING_ID].log_level;
			for (id = 0; id < ARRAYSIZE(fw_verbose_level_map); id++) {
				if ((fw_verbose_level_map[id].tag == plog_hdr->tag) &&
					(fw_verbose_level_map[id].log_level > log_level))
					return;
			}
		}
		if (plog_hdr->binary_payload) {
			DHD_ECNTR_LOG(("%d.%d EL:tag=%d len=%d fmt=0x%x",
				(uint32)(log_ptr[plog_hdr->count - 1] / EL_MSEC_PER_SEC),
				(uint32)(log_ptr[plog_hdr->count - 1] % EL_MSEC_PER_SEC),
				plog_hdr->tag,
				plog_hdr->count,
				plog_hdr->fmt_num_raw));

			for (count = 0; count < (plog_hdr->count - 1); count++) {
				/* XXX: skip first line feed in case count 0 */
				if (count && (count % LOG_PRINT_CNT_MAX == 0)) {
					DHD_ECNTR_LOG(("\n\t%08x", log_ptr[count]));
				} else {
					DHD_ECNTR_LOG((" %08x", log_ptr[count]));
				}
			}
			DHD_ECNTR_LOG(("\n"));
		}
		else {
			bcm_binit(&b, fmtstr_loc_buf, FMTSTR_SIZE);
			/* XXX: The 'hdr->count - 1' is dongle time */
#ifndef OEM_ANDROID
			bcm_bprintf(&b, "%06d.%03d EL: %d 0x%x",
				(uint32)(log_ptr[plog_hdr->count - 1] / EL_MSEC_PER_SEC),
				(uint32)(log_ptr[plog_hdr->count - 1] % EL_MSEC_PER_SEC),
				plog_hdr->tag,
				plog_hdr->fmt_num_raw);
#else
			bcm_bprintf(&b, "%06d.%03d EL:%s:%u:%u %d %d 0x%x",
				(uint32)(log_ptr[plog_hdr->count - 1] / EL_MSEC_PER_SEC),
				(uint32)(log_ptr[plog_hdr->count - 1] % EL_MSEC_PER_SEC),
				EL_PARSE_VER, logset, block,
				plog_hdr->tag,
				plog_hdr->count,
				plog_hdr->fmt_num_raw);
#endif /* !OEM_ANDROID */
			for (count = 0; count < (plog_hdr->count - 1); count++) {
				bcm_bprintf(&b, " %x", log_ptr[count]);
			}

			/* ensure preserve fw logs go to debug_dump only in case of customer4 */
			if (logset < dhdp->event_log_max_sets &&
				((0x01u << logset) & dhdp->logset_prsrv_mask)) {
				DHD_PRSRV_MEM(("%s\n", b.origbuf));
			} else {
#ifdef DHD_DEBUGABILITY_LOG_DUMP_RING
				DHD_FW_VERBOSE(("%s\n", b.origbuf));
#else
				DHD_FWLOG(("%s\n", b.origbuf));
#endif
#ifdef DHD_LOG_PRINT_RATE_LIMIT
				log_print_count++;
#endif /* DHD_LOG_PRINT_RATE_LIMIT */
			}
		}
		return;
	}

	str_buf = MALLOCZ(dhdp->osh, (MAX_NO_OF_ARG * SIZE_LOC_STR));
	if (!str_buf) {
		DHD_ERROR(("%s: malloc failed str_buf\n", __FUNCTION__));
		return;
	}

	if ((plog_hdr->fmt_num) < raw_event->num_fmts) {
		if (plog_hdr->tag == EVENT_LOG_TAG_MSCHPROFILE) {
			snprintf(fmtstr_loc_buf, FMTSTR_SIZE, "%s",
				raw_event->fmts[plog_hdr->fmt_num]);
			plog_hdr->count++;
		} else {
			snprintf(fmtstr_loc_buf, FMTSTR_SIZE, "CONSOLE_E:%u:%u %06d.%03d %s",
				logset, block,
				(uint32)(log_ptr[plog_hdr->count - 1] / EL_MSEC_PER_SEC),
				(uint32)(log_ptr[plog_hdr->count - 1] % EL_MSEC_PER_SEC),
				raw_event->fmts[plog_hdr->fmt_num]);
		}
		c_ptr = fmtstr_loc_buf;
	} else {
		/* for ecounters, don't print the error as it will flood */
		if ((plog_hdr->fmt_num != DHD_OW_BI_EVENT_FMT_NUM) &&
			(plog_hdr->fmt_num != DHD_TW_BI_EVENT_FMT_NUM)) {
			DHD_ERROR(("%s: fmt number: 0x%x out of range\n",
				__FUNCTION__, plog_hdr->fmt_num));
		} else {
			DHD_INFO(("%s: fmt number: 0x%x out of range\n",
				__FUNCTION__, plog_hdr->fmt_num));
		}

		goto exit;
	}

	if (plog_hdr->count > MAX_NO_OF_ARG) {
		DHD_ERROR(("%s: plog_hdr->count(%d) out of range\n",
			__FUNCTION__, plog_hdr->count));
		goto exit;
	}

	/* print the format string which will be needed for debugging incorrect formats */
	DHD_INFO(("%s: fmtstr_loc_buf = %s\n", __FUNCTION__, fmtstr_loc_buf));

	/* Replace all %p to %x to handle 32 bit %p */
	replace_percent_p_to_x(fmtstr_loc_buf);

	for (count = 0; count < (plog_hdr->count - 1); count++) {
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
			} else if (check_valid_non_string_format(c_ptr)) {
				/* Other than string format */
				arg[count].val = log_ptr[count];
			} else {
				/* There is nothing copied after % or improper format specifier
				 * after current %, because of not enough buffer size for complete
				 * copy of original fmt string.
				 * This is causing error mentioned below.
				 * Error: "Please remove unsupported %\x00 in format string"
				 * error(lib/vsprintf.c:1900 format_decode+0x3bc/0x470).
				 * Refer to JIRA: SWWLAN-200629 for detailed info.
				 *
				 * Terminate the string at current .
				 */
				*(c_ptr - 1) = '\0';
				break;
			}
		}
	}

	/* ensure preserve fw logs go to debug_dump only in case of customer4 */
	if (logset < dhdp->event_log_max_sets &&
			((0x01u << logset) & dhdp->logset_prsrv_mask)) {
		if (dhd_msg_level & DHD_EVENT_VAL) {
			if (dhd_msg_level & DHD_PRSRV_MEM_VAL)
				printk(fmtstr_loc_buf, arg[0], arg[1], arg[2], arg[3],
					arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10],
					arg[11], arg[12], arg[13], arg[14], arg[15]);
		}
	} else {
		if (dhd_msg_level & DHD_FWLOG_VAL) {
			printk(fmtstr_loc_buf, arg[0], arg[1], arg[2], arg[3],
				arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10],
				arg[11], arg[12], arg[13], arg[14], arg[15]);
		}
#ifdef DHD_LOG_PRINT_RATE_LIMIT
		log_print_count++;
#endif /* DHD_LOG_PRINT_RATE_LIMIT */
	}

exit:
	MFREE(dhdp->osh, str_buf, (MAX_NO_OF_ARG * SIZE_LOC_STR));
}

#if defined(EWP_BCM_TRACE) || defined(EWP_RTT_LOGGING) || \
	defined(EWP_ECNTRS_LOGGING)
static int
dhd_dbg_send_evtlog_to_ring(prcd_event_log_hdr_t *plog_hdr,
	dhd_dbg_ring_entry_t *msg_hdr, dhd_dbg_ring_t *ring,
	uint16 max_payload_len, uint8 *logbuf)
{
	event_log_hdr_t *log_hdr;
	struct tracelog_header *logentry_header;
	uint16 len_chk = 0;

	BCM_REFERENCE(log_hdr);
	BCM_REFERENCE(logentry_header);
	/*
	 * check msg hdr len before pushing.
	 * FW msg_hdr.len includes length of event log hdr,
	 * logentry header and payload.
	 */
	len_chk = (sizeof(*logentry_header) + sizeof(*log_hdr) +
			max_payload_len);
	/* account extended event log header(extended_event_log_hdr) */
	if (plog_hdr->ext_event_log_hdr) {
		len_chk += sizeof(*log_hdr);
	}
	if (msg_hdr->len > len_chk) {
		DHD_ERROR(("%s: EVENT_LOG_VALIDATION_FAILS: "
			"msg_hdr->len=%u, max allowed for %s=%u\n",
			__FUNCTION__, msg_hdr->len, ring->name, len_chk));
		return BCME_ERROR;
	}
	dhd_dbg_ring_push(ring, msg_hdr, logbuf);
	return BCME_OK;
}
#endif /* EWP_BCM_TRACE || EWP_RTT_LOGGING || EWP_ECNTRS_LOGGING */

void
dhd_dbg_msgtrace_log_parser(dhd_pub_t *dhdp, void *event_data,
	void *raw_event_ptr, uint datalen, bool msgtrace_hdr_present,
	uint32 msgtrace_seqnum)
{
	msgtrace_hdr_t *hdr;
	char *data, *tmpdata;
	const uint32 log_hdr_len = sizeof(event_log_hdr_t);
	uint32 log_pyld_len;
	static uint32 seqnum_prev = 0;
	event_log_hdr_t *log_hdr;
	bool msg_processed = FALSE;
	prcd_event_log_hdr_t prcd_log_hdr;
	prcd_event_log_hdr_t *plog_hdr;
	dll_t list_head, *cur;
	loglist_item_t *log_item;
	dhd_dbg_ring_entry_t msg_hdr;
	char *logbuf;
	struct tracelog_header *logentry_header;
	uint ring_data_len = 0;
	bool ecntr_pushed = FALSE;
	bool rtt_pushed = FALSE;
	bool bcm_trace_pushed = FALSE;
	bool dll_inited = FALSE;
	uint32 logset = 0;
	uint16 block = 0;
	bool event_log_max_sets_queried;
	uint32 event_log_max_sets;
	uint min_expected_len = 0;
	uint16 len_chk = 0;

	BCM_REFERENCE(ecntr_pushed);
	BCM_REFERENCE(rtt_pushed);
	BCM_REFERENCE(bcm_trace_pushed);
	BCM_REFERENCE(len_chk);

	/* store event_logset_queried and event_log_max_sets in local variables
	 * to avoid race conditions as they were set from different contexts(preinit)
	 */
	event_log_max_sets_queried = dhdp->event_log_max_sets_queried;
	/* Make sure queried is read first with wmb and then max_sets,
	 * as it is done in reverse order during preinit ioctls.
	 */
	OSL_SMP_WMB();
	event_log_max_sets = dhdp->event_log_max_sets;

	if (msgtrace_hdr_present)
		min_expected_len = (MSGTRACE_HDRLEN + EVENT_LOG_BLOCK_LEN);
	else
		min_expected_len = EVENT_LOG_BLOCK_LEN;

	/* log trace event consists of:
	 * msgtrace header
	 * event log block header
	 * event log payload
	 */
	if (!event_data || (datalen <= min_expected_len)) {
		DHD_ERROR(("%s: Not processing due to invalid event_data : %p or length : %d\n",
			__FUNCTION__, event_data, datalen));
		if (event_data && msgtrace_hdr_present) {
			prhex("event_data dump", event_data, datalen);
			tmpdata = (char *)event_data + MSGTRACE_HDRLEN;
			if (tmpdata) {
				DHD_ERROR(("EVENT_LOG_HDR[0x%x]: Set: 0x%08x length = %d\n",
					ltoh16(*((uint16 *)(tmpdata+2))),
					ltoh32(*((uint32 *)(tmpdata + 4))),
					ltoh16(*((uint16 *)(tmpdata)))));
			}
		} else if (!event_data) {
			DHD_ERROR(("%s: event_data is NULL, cannot dump prhex\n", __FUNCTION__));
		}

		return;
	}

	if (msgtrace_hdr_present) {
		hdr = (msgtrace_hdr_t *)event_data;
		data = (char *)event_data + MSGTRACE_HDRLEN;
		datalen -= MSGTRACE_HDRLEN;
		msgtrace_seqnum = ntoh32(hdr->seqnum);
	} else {
		data = (char *)event_data;
	}

	if (dhd_dbg_msgtrace_seqchk(&seqnum_prev, msgtrace_seqnum))
		return;

	/* Save the whole message to event log ring */
	memset(&msg_hdr, 0, sizeof(dhd_dbg_ring_entry_t));
	logbuf = VMALLOC(dhdp->osh, sizeof(*logentry_header) + datalen);
	if (logbuf == NULL)
		return;
	logentry_header = (struct tracelog_header *)logbuf;
	logentry_header->magic_num = TRACE_LOG_MAGIC_NUMBER;
	logentry_header->buf_size = datalen;
	logentry_header->seq_num = msgtrace_seqnum;
	msg_hdr.type = DBG_RING_ENTRY_DATA_TYPE;

	ring_data_len = datalen + sizeof(*logentry_header);

	if ((sizeof(*logentry_header) + datalen) > PAYLOAD_MAX_LEN) {
		DHD_ERROR(("%s:Payload len=%u exceeds max len\n", __FUNCTION__,
			((uint)sizeof(*logentry_header) + datalen)));
		goto exit;
	}

	msg_hdr.len = sizeof(*logentry_header) + datalen;
	memcpy(logbuf + sizeof(*logentry_header), data, datalen);
	DHD_DBGIF(("%s: datalen %d %d\n", __FUNCTION__, msg_hdr.len, datalen));
#ifndef DHD_DEBUGABILITY_LOG_DUMP_RING
	dhd_dbg_push_to_ring(dhdp, FW_VERBOSE_RING_ID, &msg_hdr, logbuf);
#endif
	/* Print sequence number, originating set and length of received
	 * event log buffer. Refer to event log buffer structure in
	 * event_log.h
	 */
	DHD_MSGTRACE_LOG(("EVENT_LOG_HDR[0x%x]: Set: 0x%08x length = %d\n",
		ltoh16(*((uint16 *)(data+2))), ltoh32(*((uint32 *)(data + 4))),
		ltoh16(*((uint16 *)(data)))));

	logset = ltoh32(*((uint32 *)(data + 4)));

	if (logset >= event_log_max_sets) {
		DHD_ERROR(("%s logset: %d max: %d out of range queried: %d\n",
			__FUNCTION__, logset, event_log_max_sets, event_log_max_sets_queried));
#ifdef DHD_FW_COREDUMP
		if (event_log_max_sets_queried) {
			DHD_ERROR(("%s: collect socram for DUMP_TYPE_LOGSET_BEYOND_RANGE\n",
				__FUNCTION__));
			dhdp->memdump_type = DUMP_TYPE_LOGSET_BEYOND_RANGE;
			dhd_bus_mem_dump(dhdp);
		}
#endif /* DHD_FW_COREDUMP */
	}

	block = ltoh16(*((uint16 *)(data + 2)));

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
	dll_inited = TRUE;

	while (datalen > log_hdr_len) {
		log_hdr = (event_log_hdr_t *)(data + datalen - log_hdr_len);
		memset(&prcd_log_hdr, 0, sizeof(prcd_log_hdr));
		if (!dhd_dbg_process_event_log_hdr(log_hdr, &prcd_log_hdr)) {
			DHD_ERROR(("%s: Error while parsing event log header\n",
				__FUNCTION__));
		}

		/* skip zero padding at end of frame */
		if (prcd_log_hdr.tag == EVENT_LOG_TAG_NULL) {
			datalen -= log_hdr_len;
			continue;
		}
		/* Check argument count (for non-ecounter events only),
		 * any event log should contain at least
		 * one argument (4 bytes) for arm cycle count and up to 16
		 * arguments except EVENT_LOG_TAG_STATS which could use the
		 * whole payload of 256 words
		 */
		if (prcd_log_hdr.count == 0) {
			break;
		}
		/* Both tag_stats and proxd are binary payloads so skip
		 * argument count check for these.
		 */
		if ((prcd_log_hdr.tag != EVENT_LOG_TAG_STATS) &&
			(prcd_log_hdr.tag != EVENT_LOG_TAG_PROXD_SAMPLE_COLLECT) &&
			(prcd_log_hdr.tag != EVENT_LOG_TAG_ROAM_ENHANCED_LOG) &&
			(prcd_log_hdr.tag != EVENT_LOG_TAG_BCM_TRACE) &&
			(prcd_log_hdr.count > MAX_NO_OF_ARG)) {
			break;
		}

		log_pyld_len = (prcd_log_hdr.count + prcd_log_hdr.ext_event_log_hdr) *
			DATA_UNIT_FOR_LOG_CNT;
		/* log data should not cross the event data boundary */
		if ((uint32)((char *)log_hdr - data) < log_pyld_len) {
			break;
		}
		/* skip 4 bytes time stamp packet */
		if (prcd_log_hdr.tag == EVENT_LOG_TAG_TS ||
			prcd_log_hdr.tag == EVENT_LOG_TAG_ENHANCED_TS) {
			datalen -= (log_pyld_len + log_hdr_len);
			continue;
		}
		if (!(log_item = MALLOC(dhdp->osh, sizeof(*log_item)))) {
			DHD_ERROR(("%s allocating log list item failed\n",
				__FUNCTION__));
			break;
		}

		log_item->prcd_log_hdr.tag = prcd_log_hdr.tag;
		log_item->prcd_log_hdr.count = prcd_log_hdr.count;
		log_item->prcd_log_hdr.fmt_num = prcd_log_hdr.fmt_num;
		log_item->prcd_log_hdr.fmt_num_raw = prcd_log_hdr.fmt_num_raw;
		log_item->prcd_log_hdr.armcycle = prcd_log_hdr.armcycle;
		log_item->prcd_log_hdr.log_ptr = prcd_log_hdr.log_ptr;
		log_item->prcd_log_hdr.payload_len = prcd_log_hdr.payload_len;
		log_item->prcd_log_hdr.ext_event_log_hdr = prcd_log_hdr.ext_event_log_hdr;
		log_item->prcd_log_hdr.binary_payload = prcd_log_hdr.binary_payload;

		dll_insert(&log_item->list, &list_head);
		datalen -= (log_pyld_len + log_hdr_len);
	}

	while (!dll_empty(&list_head)) {
		msg_processed = FALSE;
		cur = dll_head_p(&list_head);

		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
		log_item = (loglist_item_t *)container_of(cur, loglist_item_t, list);
		GCC_DIAGNOSTIC_POP();

		plog_hdr = &log_item->prcd_log_hdr;
#if defined(EWP_ECNTRS_LOGGING) && defined(DHD_LOG_DUMP)
		/* Ecounter tag can be time_data or log_stats+binary paloaod */
		if ((plog_hdr->tag == EVENT_LOG_TAG_ECOUNTERS_TIME_DATA) ||
			((plog_hdr->tag == EVENT_LOG_TAG_STATS) &&
			(plog_hdr->binary_payload))) {
			if (!ecntr_pushed && dhd_log_dump_ecntr_enabled()) {
				if (dhd_dbg_send_evtlog_to_ring(plog_hdr, &msg_hdr,
					dhdp->ecntr_dbg_ring,
					PAYLOAD_ECNTR_MAX_LEN, logbuf) != BCME_OK) {
					goto exit;
				}
				ecntr_pushed = TRUE;
			}
		}
#endif /* EWP_ECNTRS_LOGGING && DHD_LOG_DUMP */

		if (plog_hdr->tag == EVENT_LOG_TAG_ROAM_ENHANCED_LOG) {
			print_roam_enhanced_log(plog_hdr);
			msg_processed = TRUE;
		}
#if defined(EWP_RTT_LOGGING) && defined(DHD_LOG_DUMP)
		if ((plog_hdr->tag == EVENT_LOG_TAG_PROXD_SAMPLE_COLLECT) &&
				plog_hdr->binary_payload) {
			if (!rtt_pushed && dhd_log_dump_rtt_enabled()) {
				if (dhd_dbg_send_evtlog_to_ring(plog_hdr, &msg_hdr,
					dhdp->rtt_dbg_ring,
					PAYLOAD_RTT_MAX_LEN, logbuf) != BCME_OK) {
					goto exit;
				}
				rtt_pushed = TRUE;
			}
		}
#endif /* EWP_RTT_LOGGING && DHD_LOG_DUMP */

#ifdef EWP_BCM_TRACE
		if ((logset == EVENT_LOG_SET_BCM_TRACE) && !bcm_trace_pushed &&
			plog_hdr->binary_payload) {
			if (dhd_dbg_send_evtlog_to_ring(plog_hdr, &msg_hdr,
				dhdp->bcm_trace_dbg_ring,
				PAYLOAD_BCM_TRACE_MAX_LEN, logbuf) != BCME_OK) {
				goto exit;
			}
			bcm_trace_pushed = TRUE;
		}
#endif /* EWP_BCM_TRACE */

#if defined (DHD_EVENT_LOG_FILTER)
		if (plog_hdr->tag == EVENT_LOG_TAG_STATS) {
			dhd_event_log_filter_event_handler(dhdp, plog_hdr, plog_hdr->log_ptr);
		}
#endif /* DHD_EVENT_LOG_FILTER */
		if (!msg_processed) {
			dhd_dbg_verboselog_handler(dhdp, plog_hdr, raw_event_ptr,
				logset, block, (uint32 *)data);
		}
		dll_delete(cur);
		MFREE(dhdp->osh, log_item, sizeof(*log_item));

	}
	BCM_REFERENCE(log_hdr);
exit:
	while (dll_inited && (!dll_empty(&list_head))) {
		cur = dll_head_p(&list_head);

		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
		log_item = (loglist_item_t *)container_of(cur, loglist_item_t, list);
		GCC_DIAGNOSTIC_POP();

		dll_delete(cur);
		MFREE(dhdp->osh, log_item, sizeof(*log_item));
	}

	VMFREE(dhdp->osh, logbuf, ring_data_len);
}
#else /* !SHOW_LOGTRACE */
static INLINE void dhd_dbg_verboselog_handler(dhd_pub_t *dhdp,
	prcd_event_log_hdr_t *plog_hdr, void *raw_event_ptr, uint32 logset, uint16 block,
	uint32 *data) {};
INLINE void dhd_dbg_msgtrace_log_parser(dhd_pub_t *dhdp,
	void *event_data, void *raw_event_ptr, uint datalen,
	bool msgtrace_hdr_present, uint32 msgtrace_seqnum) {};
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
		dhd_dbg_msgtrace_log_parser(dhdp, event_data, raw_event_ptr, datalen, TRUE, 0);
}

#ifdef BTLOG
void
dhd_dbg_bt_log_handler(dhd_pub_t *dhdp, void *data, uint datalen)
{
	dhd_dbg_ring_entry_t msg_hdr;
	int ret;

	/* push to ring */
	memset(&msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.type = DBG_RING_ENTRY_DATA_TYPE;
	msg_hdr.len = datalen;
	ret = dhd_dbg_push_to_ring(dhdp, BT_LOG_RING_ID, &msg_hdr, data);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s ring push failed %d\n", __FUNCTION__, ret));
	}
}
#endif	/* BTLOG */

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
		/* DHD_ERROR(("%s set log tag iovar failed %d\n", __FUNCTION__, ret)); */
	}
}

int
dhd_dbg_set_configuration(dhd_pub_t *dhdp, int ring_id, int log_level, int flags, uint32 threshold)
{
	dhd_dbg_ring_t *ring;
	uint8 set = 1;
	int i, array_len = 0;
	struct log_level_table *log_level_tbl = NULL;
	if (!dhdp || !dhdp->dbg)
		return BCME_BADADDR;

	if (!VALID_RING(ring_id)) {
		DHD_ERROR(("%s : invalid ring_id : %d\n", __FUNCTION__, ring_id));
		return BCME_RANGE;
	}

	ring = &dhdp->dbg->dbg_rings[ring_id];
	dhd_dbg_ring_config(ring, log_level, threshold);

	if (log_level > 0)
		set = TRUE;

	if (ring->id == FW_VERBOSE_RING_ID) {
		log_level_tbl = fw_verbose_level_map;
		array_len = ARRAYSIZE(fw_verbose_level_map);
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

int
__dhd_dbg_get_ring_status(dhd_dbg_ring_t *ring, dhd_dbg_ring_status_t *get_ring_status)
{
	dhd_dbg_ring_status_t ring_status;
	int ret = BCME_OK;

	if (ring == NULL) {
		return BCME_BADADDR;
	}

	bzero(&ring_status, sizeof(dhd_dbg_ring_status_t));
	RING_STAT_TO_STATUS(ring, ring_status);
	*get_ring_status = ring_status;

	return ret;
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
	if (!dhdp || !dhdp->dbg)
		return BCME_BADADDR;
	dbg = dhdp->dbg;

	for (id = DEBUG_RING_ID_INVALID + 1; id < DEBUG_RING_ID_MAX; id++) {
		dbg_ring = &dbg->dbg_rings[id];
		if (VALID_RING(dbg_ring->id) && (dbg_ring->id == ring_id)) {
			__dhd_dbg_get_ring_status(dbg_ring, dbg_ring_status);
			break;
		}
	}
	if (!VALID_RING(id)) {
		DHD_ERROR(("%s : cannot find the ring_id : %d\n", __FUNCTION__, ring_id));
		ret = BCME_NOTFOUND;
	}
	return ret;
}

#ifdef SHOW_LOGTRACE
void
dhd_dbg_read_ring_into_trace_buf(dhd_dbg_ring_t *ring, trace_buf_info_t *trace_buf_info)
{
	dhd_dbg_ring_status_t ring_status;
	uint32 rlen = 0;

	rlen = dhd_dbg_ring_pull_single(ring, trace_buf_info->buf, TRACE_LOG_BUF_MAX_SIZE, TRUE);

	trace_buf_info->size = rlen;
	trace_buf_info->availability = NEXT_BUF_NOT_AVAIL;
	if (rlen == 0) {
		trace_buf_info->availability = BUF_NOT_AVAILABLE;
		return;
	}

	__dhd_dbg_get_ring_status(ring, &ring_status);

	if (ring_status.written_bytes != ring_status.read_bytes) {
		trace_buf_info->availability = NEXT_BUF_AVAIL;
	}
}
#endif /* SHOW_LOGTRACE */

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
				dhd_dbg_ring_start(dbg_ring);
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
		case WLFC_CTL_PKTFLAG_EXPIRED:
			pkt_fate = TX_PKT_FATE_FW_DROP_EXPTIME;
			break;
#ifndef OEM_ANDROID
		case WLFC_CTL_PKTFLAG_MKTFREE:
			pkt_fate = TX_PKT_FATE_FW_PKT_FREE;
			break;
#endif /* !OEM_ANDROID */
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

	count = 0;
	while ((count < pkt_count) && tx_pkts) {
		if (tx_pkts->info.pkt) {
			PKTFREE(dhdp->osh, tx_pkts->info.pkt, TRUE);
		}
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

	count = 0;
	while ((count < pkt_count) && rx_pkts) {
		if (rx_pkts->info.pkt) {
			PKTFREE(dhdp->osh, rx_pkts->info.pkt, TRUE);
		}
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
	uint32 alloc_len;
	int ret = BCME_OK;
	unsigned long flags;

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

	/* allocate and initialize tx packet monitoring */
	alloc_len = sizeof(*tx_report);
	tx_report = (dhd_dbg_tx_report_t *)MALLOCZ(dhdp->osh, alloc_len);
	if (unlikely(!tx_report)) {
		DHD_ERROR(("%s(): could not allocate memory for - "
			"dhd_dbg_tx_report_t\n", __FUNCTION__));
		ret = -ENOMEM;
		goto fail;
	}

	alloc_len = (sizeof(*tx_pkts) * MAX_FATE_LOG_LEN);
	tx_pkts = (dhd_dbg_tx_info_t *)MALLOCZ(dhdp->osh, alloc_len);
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
	rx_report = (dhd_dbg_rx_report_t *)MALLOCZ(dhdp->osh, alloc_len);
	if (unlikely(!rx_report)) {
		DHD_ERROR(("%s(): could not allocate memory for - "
			"dhd_dbg_rx_report_t\n", __FUNCTION__));
		ret = -ENOMEM;
		goto fail;
	}

	alloc_len = (sizeof(*rx_pkts) * MAX_FATE_LOG_LEN);
	rx_pkts = (dhd_dbg_rx_info_t *)MALLOCZ(dhdp->osh, alloc_len);
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
		alloc_len = (sizeof(*tx_pkts) * MAX_FATE_LOG_LEN);
		MFREE(dhdp->osh, tx_pkts, alloc_len);
	}
	if (tx_report) {
		alloc_len = sizeof(*tx_report);
		MFREE(dhdp->osh, tx_report, alloc_len);
	}
	dhdp->dbg->pkt_mon.tx_report = NULL;
	dhdp->dbg->pkt_mon.tx_report->tx_pkts = NULL;
	dhdp->dbg->pkt_mon.tx_pkt_mon = NULL;
	dhdp->dbg->pkt_mon.tx_status_mon = NULL;
	dhdp->dbg->pkt_mon.tx_pkt_state = PKT_MON_DETACHED;
	dhdp->dbg->pkt_mon.tx_status_state = PKT_MON_DETACHED;

	/* rx packet monitoring */
	if (rx_pkts) {
		alloc_len = (sizeof(*rx_pkts) * MAX_FATE_LOG_LEN);
		MFREE(dhdp->osh, rx_pkts, alloc_len);
	}
	if (rx_report) {
		alloc_len = sizeof(*rx_report);
		MFREE(dhdp->osh, rx_report, alloc_len);
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
					DHD_INFO(("%s(): couldn't match tx_status, pkt_pos=%u, "
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
			MFREE(dhdp->osh, tx_report->tx_pkts,
				(sizeof(*tx_report->tx_pkts) * MAX_FATE_LOG_LEN));
			dhdp->dbg->pkt_mon.tx_report->tx_pkts = NULL;
		}
		MFREE(dhdp->osh, tx_report, sizeof(*tx_report));
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
			MFREE(dhdp->osh, rx_report->rx_pkts,
				(sizeof(*rx_report->rx_pkts) * MAX_FATE_LOG_LEN));
			dhdp->dbg->pkt_mon.rx_report->rx_pkts = NULL;
		}
		MFREE(dhdp->osh, rx_report, sizeof(*rx_report));
		dhdp->dbg->pkt_mon.rx_report = NULL;
	}
	dhdp->dbg->pkt_mon.rx_pkt_mon = NULL;

	DHD_PKT_MON_UNLOCK(dhdp->dbg->pkt_mon_lock, flags);
	DHD_PKT_MON(("%s(): packet monitor detach succeeded\n", __FUNCTION__));
	return BCME_OK;
}
#endif /* DBG_PKT_MON */

#if defined(DBG_PKT_MON) || defined(DHD_PKT_LOGGING)
/*
 * XXX: WAR: Because of the overloading by DMA marker field,
 * tx_status in TX completion message cannot be used. As a WAR,
 * send d11 tx_status through unused status field of PCIe
 * completion header.
 */
bool
dhd_dbg_process_tx_status(dhd_pub_t *dhdp, void *pkt, uint32 pktid,
		uint16 status)
{
	bool pkt_fate = TRUE;
	if (dhdp->d11_tx_status) {
		pkt_fate = (status == WLFC_CTL_PKTFLAG_DISCARD) ? TRUE : FALSE;
		DHD_DBG_PKT_MON_TX_STATUS(dhdp, pkt, pktid, status);
	}
	return pkt_fate;
}
#else /* DBG_PKT_MON || DHD_PKT_LOGGING */
bool
dhd_dbg_process_tx_status(dhd_pub_t *dhdp, void *pkt,
		uint32 pktid, uint16 status)
{
	return TRUE;
}
#endif /* DBG_PKT_MON || DHD_PKT_LOGGING */

#define	EL_LOG_STR_LEN	512

#define PRINT_CHN_PER_LINE 8
#define PRINT_CHAN_LINE(cnt) \
{\
	cnt ++; \
	if (cnt >= PRINT_CHN_PER_LINE) { \
		DHD_ERROR(("%s\n", b.origbuf)); \
		bcm_binit(&b, pr_buf, EL_LOG_STR_LEN); \
		bcm_bprintf(&b, "%s: ", prefix); \
		cnt = 0; \
	} \
}

void print_roam_chan_list(char *prefix, uint chan_num, uint16 band_2g,
	uint16 uni2a, uint8 uni3, uint8 *uni2c)
{
	struct bcmstrbuf b;
	char pr_buf[EL_LOG_STR_LEN] = { 0 };
	int cnt = 0;
	int idx, idx2;

	bcm_binit(&b, pr_buf, EL_LOG_STR_LEN);
	bcm_bprintf(&b, "%s: count(%d)", prefix, chan_num);
	/* 2G channnels */
	for (idx = 0; idx < NBITS(uint16); idx++) {
		if (BCM_BIT(idx) & band_2g) {
			bcm_bprintf(&b, " %d", idx);
			PRINT_CHAN_LINE(cnt);

		}
	}

	/* 5G UNII BAND 1, UNII BAND 2A */
	for (idx = 0; idx < NBITS(uint16); idx++) {
		if (BCM_BIT(idx) & uni2a) {
			bcm_bprintf(&b, " %u", ROAM_CHN_UNI_2A + idx * ROAM_CHN_SPACE);
			PRINT_CHAN_LINE(cnt);
		}
	}

	/* 5G UNII BAND 2C */
	for (idx2 = 0; idx2 < 3; idx2++) {
		for (idx = 0; idx < NBITS(uint8); idx++) {
			if (BCM_BIT(idx) & uni2c[idx2]) {
				bcm_bprintf(&b, " %u", ROAM_CHN_UNI_2C +
					idx2 * ROAM_CHN_SPACE * NBITS(uint8) +
					idx * ROAM_CHN_SPACE);
				PRINT_CHAN_LINE(cnt);
			}
		}
	}

	/* 5G UNII BAND 3 */
	for (idx = 0; idx < NBITS(uint8); idx++) {
		if (BCM_BIT(idx) & uni3) {
			bcm_bprintf(&b, " %u", ROAM_CHN_UNI_3 + idx * ROAM_CHN_SPACE);
			PRINT_CHAN_LINE(cnt);
		}
	}

	if (cnt != 0) {
		DHD_ERROR(("%s\n", b.origbuf));
	}
}

void pr_roam_scan_start_v1(prcd_event_log_hdr_t *plog_hdr);
void pr_roam_scan_cmpl_v1(prcd_event_log_hdr_t *plog_hdr);
void pr_roam_cmpl_v1(prcd_event_log_hdr_t *plog_hdr);
void pr_roam_nbr_req_v1(prcd_event_log_hdr_t *plog_hdr);
void pr_roam_nbr_rep_v1(prcd_event_log_hdr_t *plog_hdr);
void pr_roam_bcn_req_v1(prcd_event_log_hdr_t *plog_hdr);
void pr_roam_bcn_rep_v1(prcd_event_log_hdr_t *plog_hdr);

void pr_roam_scan_start_v2(prcd_event_log_hdr_t *plog_hdr);
void pr_roam_scan_cmpl_v2(prcd_event_log_hdr_t *plog_hdr);
void pr_roam_nbr_rep_v2(prcd_event_log_hdr_t *plog_hdr);
void pr_roam_bcn_rep_v2(prcd_event_log_hdr_t *plog_hdr);
void pr_roam_btm_rep_v2(prcd_event_log_hdr_t *plog_hdr);

void pr_roam_bcn_req_v3(prcd_event_log_hdr_t *plog_hdr);
void pr_roam_bcn_rep_v3(prcd_event_log_hdr_t *plog_hdr);
void pr_roam_btm_rep_v3(prcd_event_log_hdr_t *plog_hdr);

static const pr_roam_tbl_t roam_log_print_tbl[] =
{
	{ROAM_LOG_VER_1, ROAM_LOG_SCANSTART, pr_roam_scan_start_v1},
	{ROAM_LOG_VER_1, ROAM_LOG_SCAN_CMPLT, pr_roam_scan_cmpl_v1},
	{ROAM_LOG_VER_1, ROAM_LOG_ROAM_CMPLT, pr_roam_cmpl_v1},
	{ROAM_LOG_VER_1, ROAM_LOG_NBR_REQ, pr_roam_nbr_req_v1},
	{ROAM_LOG_VER_1, ROAM_LOG_NBR_REP, pr_roam_nbr_rep_v1},
	{ROAM_LOG_VER_1, ROAM_LOG_BCN_REQ, pr_roam_bcn_req_v1},
	{ROAM_LOG_VER_1, ROAM_LOG_BCN_REP, pr_roam_bcn_rep_v1},

	{ROAM_LOG_VER_2, ROAM_LOG_SCANSTART, pr_roam_scan_start_v2},
	{ROAM_LOG_VER_2, ROAM_LOG_SCAN_CMPLT, pr_roam_scan_cmpl_v2},
	{ROAM_LOG_VER_2, ROAM_LOG_ROAM_CMPLT, pr_roam_cmpl_v1},
	{ROAM_LOG_VER_2, ROAM_LOG_NBR_REQ, pr_roam_nbr_req_v1},
	{ROAM_LOG_VER_2, ROAM_LOG_NBR_REP, pr_roam_nbr_rep_v2},
	{ROAM_LOG_VER_2, ROAM_LOG_BCN_REQ, pr_roam_bcn_req_v1},
	{ROAM_LOG_VER_2, ROAM_LOG_BCN_REP, pr_roam_bcn_rep_v2},
	{ROAM_LOG_VER_2, ROAM_LOG_BTM_REP, pr_roam_btm_rep_v2},

	{ROAM_LOG_VER_3, ROAM_LOG_SCANSTART, pr_roam_scan_start_v2},
	{ROAM_LOG_VER_3, ROAM_LOG_SCAN_CMPLT, pr_roam_scan_cmpl_v2},
	{ROAM_LOG_VER_3, ROAM_LOG_ROAM_CMPLT, pr_roam_cmpl_v1},
	{ROAM_LOG_VER_3, ROAM_LOG_NBR_REQ, pr_roam_nbr_req_v1},
	{ROAM_LOG_VER_3, ROAM_LOG_NBR_REP, pr_roam_nbr_rep_v2},
	{ROAM_LOG_VER_3, ROAM_LOG_BCN_REQ, pr_roam_bcn_req_v3},
	{ROAM_LOG_VER_3, ROAM_LOG_BCN_REP, pr_roam_bcn_rep_v3},
	{ROAM_LOG_VER_3, ROAM_LOG_BTM_REP, pr_roam_btm_rep_v3},

	{0, PRSV_PERIODIC_ID_MAX, NULL}

};

void pr_roam_scan_start_v1(prcd_event_log_hdr_t *plog_hdr)
{
	roam_log_trig_v1_t *log = (roam_log_trig_v1_t *)plog_hdr->log_ptr;

	DHD_ERROR_ROAM(("ROAM_LOG_SCANSTART time: %d,"
		" version:%d reason: %d rssi:%d cu:%d result:%d\n",
		plog_hdr->armcycle, log->hdr.version, log->reason,
		log->rssi, log->current_cu, log->result));
	if (log->reason == WLC_E_REASON_DEAUTH ||
		log->reason == WLC_E_REASON_DISASSOC) {
		DHD_ERROR_ROAM(("  ROAM_LOG_PRT_ROAM: RCVD reason:%d\n",
			log->prt_roam.rcvd_reason));
	} else if (log->reason == WLC_E_REASON_BSSTRANS_REQ) {
		DHD_ERROR_ROAM(("  ROAM_LOG_BSS_REQ: mode:%d candidate:%d token:%d "
			"duration disassoc:%d valid:%d term:%d\n",
			log->bss_trans.req_mode, log->bss_trans.nbrlist_size,
			log->bss_trans.token, log->bss_trans.disassoc_dur,
			log->bss_trans.validity_dur, log->bss_trans.bss_term_dur));
	}
}

void pr_roam_scan_cmpl_v1(prcd_event_log_hdr_t *plog_hdr)
{
	roam_log_scan_cmplt_v1_t *log = (roam_log_scan_cmplt_v1_t *)plog_hdr->log_ptr;
	char chanspec_buf[CHANSPEC_STR_LEN];
	int i;

	DHD_ERROR_ROAM(("ROAM_LOG_SCAN_CMPL: time:%d version:%d"
		"is_full:%d scan_count:%d score_delta:%d",
		plog_hdr->armcycle, log->hdr.version, log->full_scan,
		log->scan_count, log->score_delta));
	DHD_ERROR_ROAM(("  ROAM_LOG_CUR_AP: " MACDBG "rssi:%d score:%d channel:%s\n",
			MAC2STRDBG((uint8 *)&log->cur_info.addr),
			log->cur_info.rssi,
			log->cur_info.score,
			wf_chspec_ntoa_ex(log->cur_info.chanspec, chanspec_buf)));
	for (i = 0; i < log->scan_list_size; i++) {
		DHD_ERROR_ROAM(("  ROAM_LOG_CANDIDATE %d: " MACDBG
			"rssi:%d score:%d channel:%s TPUT:%dkbps\n",
			i, MAC2STRDBG((uint8 *)&log->scan_list[i].addr),
			log->scan_list[i].rssi, log->scan_list[i].score,
			wf_chspec_ntoa_ex(log->scan_list[i].chanspec,
			chanspec_buf),
			log->scan_list[i].estm_tput != ROAM_LOG_INVALID_TPUT?
			log->scan_list[i].estm_tput:0));
	}
}

void pr_roam_cmpl_v1(prcd_event_log_hdr_t *plog_hdr)
{
	roam_log_cmplt_v1_t *log = (roam_log_cmplt_v1_t *)plog_hdr->log_ptr;
	char chanspec_buf[CHANSPEC_STR_LEN];

	DHD_ERROR_ROAM(("ROAM_LOG_ROAM_CMPL: time: %d, version:%d"
		"status: %d reason: %d channel:%s retry:%d " MACDBG "\n",
		plog_hdr->armcycle, log->hdr.version, log->status, log->reason,
		wf_chspec_ntoa_ex(log->chanspec, chanspec_buf),
		log->retry, MAC2STRDBG((uint8 *)&log->addr)));
}

void pr_roam_nbr_req_v1(prcd_event_log_hdr_t *plog_hdr)
{
	roam_log_nbrreq_v1_t *log = (roam_log_nbrreq_v1_t *)plog_hdr->log_ptr;

	DHD_ERROR_ROAM(("ROAM_LOG_NBR_REQ: time: %d, version:%d token:%d\n",
		plog_hdr->armcycle, log->hdr.version, log->token));
}

void pr_roam_nbr_rep_v1(prcd_event_log_hdr_t *plog_hdr)
{
	roam_log_nbrrep_v1_t *log = (roam_log_nbrrep_v1_t *)plog_hdr->log_ptr;

	DHD_ERROR_ROAM(("ROAM_LOG_NBR_REP: time:%d, veresion:%d chan_num:%d\n",
		plog_hdr->armcycle, log->hdr.version, log->channel_num));
}

void pr_roam_bcn_req_v1(prcd_event_log_hdr_t *plog_hdr)
{
	roam_log_bcnrpt_req_v1_t *log = (roam_log_bcnrpt_req_v1_t *)plog_hdr->log_ptr;

	DHD_ERROR_ROAM(("ROAM_LOG_BCN_REQ: time:%d, version:%d ret:%d"
		"class:%d num_chan:%d ",
		plog_hdr->armcycle, log->hdr.version,
		log->result, log->reg, log->channel));
	DHD_ERROR_ROAM(("ROAM_LOG_BCN_REQ: mode:%d is_wild:%d duration:%d"
		"ssid_len:%d\n", log->mode, log->bssid_wild,
		log->duration, log->ssid_len));
}

void pr_roam_bcn_rep_v1(prcd_event_log_hdr_t *plog_hdr)
{
	roam_log_bcnrpt_rep_v1_t *log = (roam_log_bcnrpt_rep_v1_t *)plog_hdr->log_ptr;
	DHD_ERROR_ROAM(("ROAM_LOG_BCN_REP: time:%d, verseion:%d count:%d\n",
		plog_hdr->armcycle, log->hdr.version,
		log->count));
}

void pr_roam_scan_start_v2(prcd_event_log_hdr_t *plog_hdr)
{
	roam_log_trig_v2_t *log = (roam_log_trig_v2_t *)plog_hdr->log_ptr;
	DHD_ERROR_ROAM(("ROAM_LOG_SCANSTART time: %d,"
		" version:%d reason: %d rssi:%d cu:%d result:%d full_scan:%d\n",
		plog_hdr->armcycle, log->hdr.version, log->reason,
		log->rssi, log->current_cu, log->result,
		log->result?(-1):log->full_scan));
	if (log->reason == WLC_E_REASON_DEAUTH ||
		log->reason == WLC_E_REASON_DISASSOC) {
		DHD_ERROR_ROAM(("  ROAM_LOG_PRT_ROAM: RCVD reason:%d\n",
			log->prt_roam.rcvd_reason));
	} else if (log->reason == WLC_E_REASON_BSSTRANS_REQ) {
		DHD_ERROR_ROAM(("  ROAM_LOG_BSS_REQ: mode:%d candidate:%d token:%d "
			"duration disassoc:%d valid:%d term:%d\n",
			log->bss_trans.req_mode, log->bss_trans.nbrlist_size,
			log->bss_trans.token, log->bss_trans.disassoc_dur,
			log->bss_trans.validity_dur, log->bss_trans.bss_term_dur));
	} else if (log->reason == WLC_E_REASON_LOW_RSSI) {
		DHD_ERROR_ROAM((" ROAM_LOG_LOW_RSSI: threshold:%d\n",
			log->low_rssi.rssi_threshold));
	}
}

void pr_roam_scan_cmpl_v2(prcd_event_log_hdr_t *plog_hdr)
{
	int i;
	roam_log_scan_cmplt_v2_t *log = (roam_log_scan_cmplt_v2_t *)plog_hdr->log_ptr;
	char chanspec_buf[CHANSPEC_STR_LEN];

	DHD_ERROR_ROAM(("ROAM_LOG_SCAN_CMPL: time:%d version:%d"
		"scan_count:%d score_delta:%d",
		plog_hdr->armcycle, log->hdr.version,
		log->scan_count, log->score_delta));
	DHD_ERROR_ROAM(("  ROAM_LOG_CUR_AP: " MACDBG "rssi:%d score:%d channel:%s\n",
			MAC2STRDBG((uint8 *)&log->cur_info.addr),
			log->cur_info.rssi,
			log->cur_info.score,
			wf_chspec_ntoa_ex(log->cur_info.chanspec, chanspec_buf)));
	for (i = 0; i < log->scan_list_size; i++) {
		DHD_ERROR_ROAM(("  ROAM_LOG_CANDIDATE %d: " MACDBG
			"rssi:%d score:%d cu :%d channel:%s TPUT:%dkbps\n",
			i, MAC2STRDBG((uint8 *)&log->scan_list[i].addr),
			log->scan_list[i].rssi, log->scan_list[i].score,
			log->scan_list[i].cu * 100 / WL_MAX_CHANNEL_USAGE,
			wf_chspec_ntoa_ex(log->scan_list[i].chanspec,
			chanspec_buf),
			log->scan_list[i].estm_tput != ROAM_LOG_INVALID_TPUT?
			log->scan_list[i].estm_tput:0));
	}
	if (log->chan_num != 0) {
		print_roam_chan_list("ROAM_LOG_SCAN_CHANLIST", log->chan_num,
			log->band2g_chan_list, log->uni2a_chan_list,
			log->uni3_chan_list, log->uni2c_chan_list);
	}

}

void pr_roam_nbr_rep_v2(prcd_event_log_hdr_t *plog_hdr)
{
	roam_log_nbrrep_v2_t *log = (roam_log_nbrrep_v2_t *)plog_hdr->log_ptr;
	DHD_ERROR_ROAM(("ROAM_LOG_NBR_REP: time:%d, veresion:%d chan_num:%d\n",
		plog_hdr->armcycle, log->hdr.version, log->channel_num));
	if (log->channel_num != 0) {
		print_roam_chan_list("ROAM_LOG_NBR_REP_CHANLIST", log->channel_num,
			log->band2g_chan_list, log->uni2a_chan_list,
			log->uni3_chan_list, log->uni2c_chan_list);
	}
}

void pr_roam_bcn_rep_v2(prcd_event_log_hdr_t *plog_hdr)
{
	roam_log_bcnrpt_rep_v2_t *log = (roam_log_bcnrpt_rep_v2_t *)plog_hdr->log_ptr;

	DHD_ERROR_ROAM(("ROAM_LOG_BCN_REP: time:%d, verseion:%d count:%d mode:%d\n",
		plog_hdr->armcycle, log->hdr.version,
		log->count, log->reason));
}

void pr_roam_btm_rep_v2(prcd_event_log_hdr_t *plog_hdr)
{
	roam_log_btm_rep_v2_t *log = (roam_log_btm_rep_v2_t *)plog_hdr->log_ptr;
	DHD_ERROR_ROAM(("ROAM_LOG_BTM_REP: time:%d version:%d req_mode:%d "
		"status:%d ret:%d\n",
		plog_hdr->armcycle, log->hdr.version,
		log->req_mode, log->status, log->result));
}

void pr_roam_bcn_req_v3(prcd_event_log_hdr_t *plog_hdr)
{
	roam_log_bcnrpt_req_v3_t *log = (roam_log_bcnrpt_req_v3_t *)plog_hdr->log_ptr;

	DHD_ERROR_ROAM(("ROAM_LOG_BCN_REQ: time:%d, version:%d ret:%d"
		"class:%d %s ",
		plog_hdr->armcycle, log->hdr.version,
		log->result, log->reg, log->channel?"":"all_chan"));
	DHD_ERROR_ROAM(("ROAM_LOG_BCN_REQ: mode:%d is_wild:%d duration:%d"
		"ssid_len:%d\n", log->mode, log->bssid_wild,
		log->duration, log->ssid_len));
	if (log->channel_num != 0) {
		print_roam_chan_list("ROAM_LOG_BCNREQ_SCAN_CHANLIST", log->channel_num,
			log->band2g_chan_list, log->uni2a_chan_list,
			log->uni3_chan_list, log->uni2c_chan_list);
	}
}

static const char*
pr_roam_bcn_rep_reason(uint16 reason_detail)
{
	static const char* reason_tbl[] = {
		"BCNRPT_RSN_SUCCESS",
		"BCNRPT_RSN_BADARG",
		"BCNRPT_RSN_SCAN_ING",
		"BCNRPT_RSN_SCAN_FAIL",
		"UNKNOWN"
	};

	if (reason_detail >= ARRAYSIZE(reason_tbl)) {
		DHD_ERROR_ROAM(("UNKNOWN Reason:%u\n", reason_detail));
		ASSERT(0);
		reason_detail = ARRAYSIZE(reason_tbl) - 1;

	}
	return reason_tbl[reason_detail];
}

void pr_roam_bcn_rep_v3(prcd_event_log_hdr_t *plog_hdr)
{
	roam_log_bcnrpt_rep_v3_t *log = (roam_log_bcnrpt_rep_v3_t *)plog_hdr->log_ptr;

	DHD_ERROR_ROAM(("ROAM_LOG_BCN_REP: time:%d, verseion:%d count:%d mode:%d\n",
		plog_hdr->armcycle, log->hdr.version,
		log->count, log->reason));
	DHD_ERROR_ROAM(("ROAM_LOG_BCN_REP: mode reason(%d):%s scan_stus:%u duration:%u\n",
		log->reason_detail, pr_roam_bcn_rep_reason(log->reason_detail),
		(log->reason_detail == BCNRPT_RSN_SCAN_FAIL)? log->scan_status:0,
		log->duration));
}

void pr_roam_btm_rep_v3(prcd_event_log_hdr_t *plog_hdr)
{
	roam_log_btm_rep_v3_t *log = (roam_log_btm_rep_v3_t *)plog_hdr->log_ptr;
	DHD_ERROR_ROAM(("ROAM_LOG_BTM_REP: time:%d version:%d req_mode:%d "
		"status:%d ret:%d target:" MACDBG "\n",
		plog_hdr->armcycle, log->hdr.version,
		log->req_mode, log->status, log->result,
		MAC2STRDBG((uint8 *)&log->target_addr)));
}

void
print_roam_enhanced_log(prcd_event_log_hdr_t *plog_hdr)
{
	prsv_periodic_log_hdr_t *hdr = (prsv_periodic_log_hdr_t *)plog_hdr->log_ptr;
	uint32 *ptr = (uint32 *)plog_hdr->log_ptr;
	int i;
	int loop_cnt = hdr->length / sizeof(uint32);
	struct bcmstrbuf b;
	char pr_buf[EL_LOG_STR_LEN] = { 0 };
	const pr_roam_tbl_t *cur_elem = &roam_log_print_tbl[0];

	while (cur_elem && cur_elem->pr_func) {
		if (hdr->version == cur_elem->version &&
			hdr->id == cur_elem->id) {
			cur_elem->pr_func(plog_hdr);
			return;
		}
		cur_elem++;
	}

	bcm_binit(&b, pr_buf, EL_LOG_STR_LEN);
	bcm_bprintf(&b, "ROAM_LOG_UNKNOWN ID:%d ver:%d armcycle:%d",
		hdr->id, hdr->version, plog_hdr->armcycle);
	for (i = 0; i < loop_cnt && b.size > 0; i++) {
		bcm_bprintf(&b, " %x", *ptr);
		ptr++;
	}
	DHD_ERROR_ROAM(("%s\n", b.origbuf));
}

/*
 * dhd_dbg_attach: initialziation of dhd dbugability module
 *
 * Return: An error code or 0 on success.
 */
#ifdef DHD_DEBUGABILITY_LOG_DUMP_RING
struct dhd_dbg_ring_buf g_ring_buf;
#endif /* DHD_DEBUGABILITY_LOG_DUMP_RING */
int
dhd_dbg_attach(dhd_pub_t *dhdp, dbg_pullreq_t os_pullreq,
	dbg_urgent_noti_t os_urgent_notifier, void *os_priv)
{
	dhd_dbg_t *dbg = NULL;
	dhd_dbg_ring_t *ring = NULL;
	int ret = BCME_ERROR, ring_id = 0;
	void *buf = NULL;
#ifdef DHD_DEBUGABILITY_LOG_DUMP_RING
	struct dhd_dbg_ring_buf *ring_buf;
#endif /* DHD_DEBUGABILITY_LOG_DUMP_RING */

	dbg = MALLOCZ(dhdp->osh, sizeof(dhd_dbg_t));
	if (!dbg)
		return BCME_NOMEM;

#ifdef CONFIG_DHD_USE_STATIC_BUF
	buf = DHD_OS_PREALLOC(dhdp, DHD_PREALLOC_FW_VERBOSE_RING, FW_VERBOSE_RING_SIZE);
#else
	buf = MALLOCZ(dhdp->osh, FW_VERBOSE_RING_SIZE);
#endif
	if (!buf)
		goto error;
	ret = dhd_dbg_ring_init(dhdp, &dbg->dbg_rings[FW_VERBOSE_RING_ID], FW_VERBOSE_RING_ID,
			(uint8 *)FW_VERBOSE_RING_NAME, FW_VERBOSE_RING_SIZE, buf, FALSE);
	if (ret)
		goto error;

#ifdef CONFIG_DHD_USE_STATIC_BUF
	buf = DHD_OS_PREALLOC(dhdp, DHD_PREALLOC_DHD_EVENT_RING, DHD_EVENT_RING_SIZE);
#else
	buf = MALLOCZ(dhdp->osh, DHD_EVENT_RING_SIZE);
#endif
	if (!buf)
		goto error;
	ret = dhd_dbg_ring_init(dhdp, &dbg->dbg_rings[DHD_EVENT_RING_ID], DHD_EVENT_RING_ID,
			(uint8 *)DHD_EVENT_RING_NAME, DHD_EVENT_RING_SIZE, buf, FALSE);
	if (ret)
		goto error;

#ifdef DHD_DEBUGABILITY_LOG_DUMP_RING
	buf = MALLOCZ(dhdp->osh, DRIVER_LOG_RING_SIZE);
	if (!buf)
		goto error;
	ret = dhd_dbg_ring_init(dhdp, &dbg->dbg_rings[DRIVER_LOG_RING_ID], DRIVER_LOG_RING_ID,
			(uint8 *)DRIVER_LOG_RING_NAME, DRIVER_LOG_RING_SIZE, buf, FALSE);
	if (ret)
		goto error;

	buf = MALLOCZ(dhdp->osh, ROAM_STATS_RING_SIZE);
	if (!buf)
		goto error;
	ret = dhd_dbg_ring_init(dhdp, &dbg->dbg_rings[ROAM_STATS_RING_ID], ROAM_STATS_RING_ID,
			(uint8 *)ROAM_STATS_RING_NAME, ROAM_STATS_RING_SIZE, buf, FALSE);
	if (ret)
		goto error;
#endif /* DHD_DEBUGABILITY_LOG_DUMP_RING */
#ifdef BTLOG
	buf = MALLOCZ(dhdp->osh, BT_LOG_RING_SIZE);
	if (!buf)
		goto error;
	ret = dhd_dbg_ring_init(dhdp, &dbg->dbg_rings[BT_LOG_RING_ID], BT_LOG_RING_ID,
			BT_LOG_RING_NAME, BT_LOG_RING_SIZE, buf, FALSE);
	if (ret)
		goto error;
#endif	/* BTLOG */

	dbg->private = os_priv;
	dbg->pullreq = os_pullreq;
	dbg->urgent_notifier = os_urgent_notifier;
	dhdp->dbg = dbg;
#ifdef DHD_DEBUGABILITY_LOG_DUMP_RING
	ring_buf = &g_ring_buf;
	ring_buf->dhd_pub = dhdp;
#endif /* DHD_DEBUGABILITY_LOG_DUMP_RING */
	return BCME_OK;

error:
	for (ring_id = DEBUG_RING_ID_INVALID + 1; ring_id < DEBUG_RING_ID_MAX; ring_id++) {
		if (VALID_RING(dbg->dbg_rings[ring_id].id)) {
			ring = &dbg->dbg_rings[ring_id];
			dhd_dbg_ring_deinit(dhdp, ring);
			if (ring->ring_buf) {
#ifndef CONFIG_DHD_USE_STATIC_BUF
				MFREE(dhdp->osh, ring->ring_buf, ring->ring_size);
#endif
				ring->ring_buf = NULL;
			}
			ring->ring_size = 0;
		}
	}
	MFREE(dhdp->osh, dbg, sizeof(dhd_dbg_t));
#ifdef DHD_DEBUGABILITY_LOG_DUMP_RING
	ring_buf = &g_ring_buf;
	ring_buf->dhd_pub = NULL;
#endif /* DHD_DEBUGABILITY_LOG_DUMP_RING */

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
	dhd_dbg_ring_t *ring = NULL;
#ifdef DHD_DEBUGABILITY_LOG_DUMP_RING
	struct dhd_dbg_ring_buf *ring_buf;
#endif /* DHD_DEBUGABILITY_LOG_DUMP_RING */

	if (!dhdp->dbg)
		return;

	dbg = dhdp->dbg;
	for (ring_id = DEBUG_RING_ID_INVALID + 1; ring_id < DEBUG_RING_ID_MAX; ring_id++) {
		if (VALID_RING(dbg->dbg_rings[ring_id].id)) {
			ring = &dbg->dbg_rings[ring_id];
			dhd_dbg_ring_deinit(dhdp, ring);
			if (ring->ring_buf) {
#ifndef CONFIG_DHD_USE_STATIC_BUF
				MFREE(dhdp->osh, ring->ring_buf, ring->ring_size);
#endif
				ring->ring_buf = NULL;
			}
			ring->ring_size = 0;
		}
	}
	MFREE(dhdp->osh, dhdp->dbg, sizeof(dhd_dbg_t));
#ifdef DHD_DEBUGABILITY_LOG_DUMP_RING
	ring_buf = &g_ring_buf;
	ring_buf->dhd_pub = NULL;
#endif /* DHD_DEBUGABILITY_LOG_DUMP_RING */
}

uint32
dhd_dbg_get_fwverbose(dhd_pub_t *dhdp)
{
	if (dhdp && dhdp->dbg) {
		return dhdp->dbg->dbg_rings[FW_VERBOSE_RING_ID].log_level;
	}
	return 0;
}

void
dhd_dbg_set_fwverbose(dhd_pub_t *dhdp, uint32 new_val)
{

	if (dhdp && dhdp->dbg) {
		dhdp->dbg->dbg_rings[FW_VERBOSE_RING_ID].log_level = new_val;
	}
}
