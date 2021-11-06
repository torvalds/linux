/*
 * DHD debugability packet logging support
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
#include <bcmstdlib_s.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <dhd_pktlog.h>
#include <dhd_wlfc.h>

#ifdef DHD_COMPACT_PKT_LOG
#include <bcmip.h>
#include <bcmudp.h>
#include <bcmdhcp.h>
#include <bcmarp.h>
#include <bcmicmp.h>
#include <bcmtlv.h>
#include <802.11.h>
#include <eap.h>
#include <eapol.h>
#include <bcmendian.h>
#include <bcm_l2_filter.h>
#include <dhd_bitpack.h>
#include <bcmipv6.h>
#endif	/* DHD_COMPACT_PKT_LOG */

#ifdef DHD_PKT_LOGGING
#ifndef strtoul
#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))
#endif /* strtoul */
extern int wl_pattern_atoh(char *src, char *dst);
extern int pattern_atoh_len(char *src, char *dst, int len);
extern wifi_tx_packet_fate __dhd_dbg_map_tx_status_to_pkt_fate(uint16 status);

#ifdef DHD_COMPACT_PKT_LOG
#define CPKT_LOG_BITS_PER_BYTE		8

#define CPKT_LOG_BIT_LEN_TYPE		4

#define CPKT_LOG_BIT_OFFSET_TS		0
#define CPKT_LOG_BIT_OFFSET_DIR		5
#define CPKT_LOG_BIT_OFFSET_TYPE	6
#define CPKT_LOG_BIT_OFFSET_SUBTYPE	10
#define CPKT_LOG_BIT_OFFSET_PKT_FATE	18

#define CPKT_LOG_BIT_MASK_TS		0x1f
#define CPKT_LOG_BIT_MASK_DIR		0x01
#define CPKT_LOG_BIT_MASK_TYPE		0x0f
#define CPKT_LOG_BIT_MASK_SUBTYPE	0xff
#define CPKT_LOG_BIT_MASK_PKT_FATE	0x0f

#define CPKT_LOG_DNS_PORT_CLIENT	53
#define CPKT_LOG_MDNS_PORT_CLIENT	5353

#define CPKT_LOG_TYPE_DNS		0x0
#define CPKT_LOG_TYPE_ARP		0x1
#define CPKT_LOG_TYPE_ICMP_REQ		0x2
#define CPKT_LOG_TYPE_ICMP_RES		0x3
#define CPKT_LOG_TYPE_ICMP_UNREACHABLE	0x4
#define CPKT_LOG_TYPE_DHCP		0x5
#define CPKT_LOG_TYPE_802_1X		0x6
#define CPKT_LOG_TYPE_ICMPv6		0x7
#define CPKT_LOG_TYPE_OTHERS		0xf

#define CPKT_LOG_802_1X_SUBTYPE_IDENTITY	0x0
#define CPKT_LOG_802_1X_SUBTYPE_TLS		0x1
#define CPKT_LOG_802_1X_SUBTYPE_TTLS		0x2
#define CPKT_LOG_802_1X_SUBTYPE_PEAP		0x3
#define CPKT_LOG_802_1X_SUBTYPE_FAST		0x4
#define CPKT_LOG_802_1X_SUBTYPE_LEAP		0x5
#define CPKT_LOG_802_1X_SUBTYPE_PWD		0x6
#define CPKT_LOG_802_1X_SUBTYPE_SIM		0x7
#define CPKT_LOG_802_1X_SUBTYPE_AKA		0x8
#define CPKT_LOG_802_1X_SUBTYPE_AKAP		0x9
#define CPKT_LOG_802_1X_SUBTYPE_SUCCESS		0xA
#define CPKT_LOG_802_1X_SUBTYPE_4WAY_M1		0xB
#define CPKT_LOG_802_1X_SUBTYPE_4WAY_M2		0xC
#define CPKT_LOG_802_1X_SUBTYPE_4WAY_M3		0xD
#define CPKT_LOG_802_1X_SUBTYPE_4WAY_M4		0xE
#define CPKT_LOG_802_1X_SUBTYPE_OTHERS		0xF

#define CPKT_LOG_DHCP_MAGIC_COOKIE_LEN			4

#define CPKT_LOG_ICMP_TYPE_DEST_UNREACHABLE		3
#define CPKT_LOG_ICMP_TYPE_DEST_UNREACHABLE_IPV4_OFFSET	4

typedef struct dhd_cpkt_log_ts_node {
	struct rb_node rb;

	uint64 ts_diff;		/* key, usec */
	int idx;
} dhd_cpkt_log_ts_node_t;

/* Compact Packet Log Timestamp values, unit: uSec */
const uint64 dhd_cpkt_log_tt_idx[] = {
	10000, 50000, 100000, 150000, 300000, 500000, 750000, 1000000, 3000000, 5000000, 7500000,
	10000000, 12500000, 15000000, 17500000, 20000000, 22500000, 25000000, 27500000, 30000000,
	32500000, 35000000, 37500000, 40000000, 50000000, 75000000, 150000000, 300000000, 400000000,
	500000000, 600000000
};
#define CPKT_LOG_TT_IDX_ARR_SZ	ARRAYSIZE(dhd_cpkt_log_tt_idx)

static int dhd_cpkt_log_init_tt(dhd_pub_t *dhdp);
static void dhd_cpkt_log_deinit_tt(dhd_pub_t *dhdp);
#endif	/* DHD_COMPACT_PKT_LOG */

int
dhd_os_attach_pktlog(dhd_pub_t *dhdp)
{
	dhd_pktlog_t *pktlog;

	if (!dhdp) {
		DHD_ERROR(("%s(): dhdp is NULL\n", __FUNCTION__));
		return -EINVAL;
	}

	pktlog = (dhd_pktlog_t *)MALLOCZ(dhdp->osh, sizeof(dhd_pktlog_t));
	if (unlikely(!pktlog)) {
		DHD_ERROR(("%s(): could not allocate memory for - "
					"dhd_pktlog_t\n", __FUNCTION__));
		return BCME_ERROR;
	}

	dhdp->pktlog = pktlog;
	pktlog->dhdp = dhdp;

	OSL_ATOMIC_INIT(dhdp->osh, &pktlog->pktlog_status);

	/* pktlog ring */
	dhdp->pktlog->pktlog_ring = dhd_pktlog_ring_init(dhdp, MIN_PKTLOG_LEN);
	dhdp->pktlog->pktlog_filter = dhd_pktlog_filter_init(MAX_DHD_PKTLOG_FILTER_LEN);
#ifdef DHD_COMPACT_PKT_LOG
	dhd_cpkt_log_init_tt(dhdp);
#endif

	DHD_ERROR(("%s(): dhd_os_attach_pktlog attach\n", __FUNCTION__));

	return BCME_OK;
}

int
dhd_os_detach_pktlog(dhd_pub_t *dhdp)
{
	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	dhd_pktlog_ring_deinit(dhdp, dhdp->pktlog->pktlog_ring);
	dhd_pktlog_filter_deinit(dhdp->pktlog->pktlog_filter);
#ifdef DHD_COMPACT_PKT_LOG
	dhd_cpkt_log_deinit_tt(dhdp);
#endif	/* DHD_COMPACT_PKT_LOG */

	DHD_ERROR(("%s(): dhd_os_attach_pktlog detach\n", __FUNCTION__));

	MFREE(dhdp->osh, dhdp->pktlog, sizeof(dhd_pktlog_t));

	return BCME_OK;
}

dhd_pktlog_ring_t*
dhd_pktlog_ring_init(dhd_pub_t *dhdp, int size)
{
	dhd_pktlog_ring_t *ring;
	int i = 0;

	if (!dhdp) {
		DHD_ERROR(("%s(): dhdp is NULL\n", __FUNCTION__));
		return NULL;
	}

	ring = (dhd_pktlog_ring_t *)MALLOCZ(dhdp->osh, sizeof(dhd_pktlog_ring_t));
	if (unlikely(!ring)) {
		DHD_ERROR(("%s(): could not allocate memory for - "
					"dhd_pktlog_ring_t\n", __FUNCTION__));
		goto fail;
	}

	dll_init(&ring->ring_info_head);
	dll_init(&ring->ring_info_free);

	ring->ring_info_mem = (dhd_pktlog_ring_info_t *)MALLOCZ(dhdp->osh,
		sizeof(dhd_pktlog_ring_info_t) * size);
	if (unlikely(!ring->ring_info_mem)) {
		DHD_ERROR(("%s(): could not allocate memory for - "
					"dhd_pktlog_ring_info_t\n", __FUNCTION__));
		goto fail;
	}

	/* initialize free ring_info linked list */
	for (i = 0; i < size; i++) {
	    dll_append(&ring->ring_info_free, (dll_t *)&ring->ring_info_mem[i].p_info);
	}

	OSL_ATOMIC_SET(dhdp->osh, &ring->start, TRUE);
	ring->pktlog_minmize = FALSE;
	ring->pktlog_len = size;
	ring->pktcount = 0;
	ring->dhdp = dhdp;
	ring->pktlog_ring_lock = osl_spin_lock_init(dhdp->osh);

	DHD_ERROR(("%s(): pktlog ring init success\n", __FUNCTION__));

	return ring;
fail:
	if (ring) {
		MFREE(dhdp->osh, ring, sizeof(dhd_pktlog_ring_t));
	}

	return NULL;
}

/* Maximum wait counts */
#define DHD_PKTLOG_WAIT_MAXCOUNT 1000
int
dhd_pktlog_ring_deinit(dhd_pub_t *dhdp, dhd_pktlog_ring_t *ring)
{
	int ret = BCME_OK;
	dhd_pktlog_ring_info_t *ring_info;
	dll_t *item, *next_p;
	int waitcounts = 0;

	if (!ring) {
		DHD_ERROR(("%s(): ring is NULL\n", __FUNCTION__));
		return -EINVAL;
	}

	if (!ring->dhdp) {
		DHD_ERROR(("%s(): dhdp is NULL\n", __FUNCTION__));
		return -EINVAL;
	}

	/* stop pkt log */
	OSL_ATOMIC_SET(dhdp->osh, &ring->start, FALSE);

	/* waiting TX/RX/TXS context is done, max timeout 1 second */
	while ((waitcounts++ < DHD_PKTLOG_WAIT_MAXCOUNT)) {
		if (!OSL_ATOMIC_READ(dhdp->osh, &dhdp->pktlog->pktlog_status))
			break;
		OSL_SLEEP(1);
	}

	if (waitcounts >= DHD_PKTLOG_WAIT_MAXCOUNT) {
		DHD_ERROR(("%s(): pktlog wait timeout pktlog_status : 0x%x \n",
			__FUNCTION__,
			OSL_ATOMIC_READ(dhdp->osh, &dhdp->pktlog->pktlog_status)));
		ASSERT(0);
		return -EINVAL;
	}

	/* free ring_info->info.pkt */
	for (item = dll_head_p(&ring->ring_info_head); !dll_end(&ring->ring_info_head, item);
		item = next_p) {
		next_p = dll_next_p(item);

		ring_info = (dhd_pktlog_ring_info_t *)item;

		if (ring_info->info.pkt) {
			PKTFREE(ring->dhdp->osh, ring_info->info.pkt, TRUE);
			DHD_PKT_LOG(("%s(): pkt free pos %p\n",
					__FUNCTION__, ring_info->info.pkt));
		}
	}

	if (ring->ring_info_mem) {
		MFREE(ring->dhdp->osh, ring->ring_info_mem,
			sizeof(dhd_pktlog_ring_info_t) * ring->pktlog_len);
	}

	if (ring->pktlog_ring_lock) {
		osl_spin_lock_deinit(ring->dhdp->osh, ring->pktlog_ring_lock);
	}

	MFREE(dhdp->osh, ring, sizeof(dhd_pktlog_ring_t));

	DHD_ERROR(("%s(): pktlog ring deinit\n", __FUNCTION__));

	return ret;
}

/*
 * dhd_pktlog_ring_add_pkts : add filtered packets into pktlog ring
 * pktid : incase of rx, pktid is not used (pass DHD_INVALID_PKID)
 * direction :  1 - TX / 0 - RX / 2 - RX Wakeup Packet
 */
int
dhd_pktlog_ring_add_pkts(dhd_pub_t *dhdp, void *pkt, void *pktdata, uint32 pktid, uint32 direction)
{
	dhd_pktlog_ring_info_t *pkts;
	dhd_pktlog_ring_t *pktlog_ring;
	dhd_pktlog_filter_t *pktlog_filter;
	u64 ts_nsec;
	uint32 pktlog_case = 0;
	unsigned long rem_nsec;
	unsigned long flags = 0;

	/*
	 * dhdp, dhdp->pktlog, dhd->pktlog_ring, pktlog_ring->start
	 * are validated from the DHD_PKTLOG_TX macro
	 */

	pktlog_ring = dhdp->pktlog->pktlog_ring;
	pktlog_filter = dhdp->pktlog->pktlog_filter;

	if (direction == PKT_TX) {
		pktlog_case = PKTLOG_TXPKT_CASE;
	} else if ((direction == PKT_RX) || (direction == PKT_WAKERX)) {
		pktlog_case = PKTLOG_RXPKT_CASE;
	}

	if ((direction != PKT_WAKERX) &&
		dhd_pktlog_filter_matched(pktlog_filter, pktdata, pktlog_case)
		== FALSE) {
		return BCME_OK;
	}

	if (direction == PKT_TX && pktid == DHD_INVALID_PKTID) {
		DHD_ERROR(("%s : Invalid PKTID \n", __FUNCTION__));
		return BCME_ERROR;
	}

	/* get free ring_info and insert to ring_info_head */
	DHD_PKT_LOG_LOCK(pktlog_ring->pktlog_ring_lock, flags);
	/* if free_list is empty, use the oldest ring_info */
	if (dll_empty(&pktlog_ring->ring_info_free)) {
		pkts = (dhd_pktlog_ring_info_t *)dll_head_p(&pktlog_ring->ring_info_head);
		dll_delete((dll_t *)pkts);
		/* free the oldest packet */
		PKTFREE(pktlog_ring->dhdp->osh, pkts->info.pkt, TRUE);
		pktlog_ring->pktcount--;
	} else {
		pkts = (dhd_pktlog_ring_info_t *)dll_tail_p(&pktlog_ring->ring_info_free);
		dll_delete((dll_t *)pkts);
	}

	/* Update packet information */
	ts_nsec = local_clock();
	rem_nsec = do_div(ts_nsec, NSEC_PER_SEC);

	pkts->info.pkt = PKTDUP(dhdp->osh, pkt);
	pkts->info.pkt_len = PKTLEN(dhdp->osh, pkt);
	pkts->info.driver_ts_sec = (uint32)ts_nsec;
	pkts->info.driver_ts_usec = (uint32)(rem_nsec/NSEC_PER_USEC);
	pkts->info.firmware_ts = 0U;
	pkts->info.payload_type = FRAME_TYPE_ETHERNET_II;
	pkts->info.direction = direction;

	if (direction == PKT_TX) {
		pkts->info.pkt_hash =  __dhd_dbg_pkt_hash((uintptr_t)pkt, pktid);
		pkts->tx_fate = TX_PKT_FATE_DRV_QUEUED;
	} else if (direction == PKT_RX) {
		pkts->info.pkt_hash = 0U;
		pkts->rx_fate = RX_PKT_FATE_SUCCESS;
	} else if (direction == PKT_WAKERX) {
		pkts->info.pkt_hash = 0U;
		pkts->rx_fate = RX_PKT_FATE_WAKE_PKT;
	}

	DHD_PKT_LOG(("%s(): pkt hash %d\n", __FUNCTION__, pkts->info.pkt_hash));
	DHD_PKT_LOG(("%s(): sec %d usec %d\n", __FUNCTION__,
		pkts->info.driver_ts_sec, pkts->info.driver_ts_usec));

	/* insert tx_pkts to the pktlog_ring->ring_info_head */
	dll_append(&pktlog_ring->ring_info_head, (dll_t *)pkts);
	pktlog_ring->pktcount++;
	DHD_PKT_LOG_UNLOCK(pktlog_ring->pktlog_ring_lock, flags);
	return BCME_OK;
}

int
dhd_pktlog_ring_tx_status(dhd_pub_t *dhdp, void *pkt, void *pktdata, uint32 pktid,
		uint16 status)
{
	dhd_pktlog_ring_info_t *tx_pkt;
	wifi_tx_packet_fate pkt_fate;
	uint32 pkt_hash, temp_hash;
	dhd_pktlog_ring_t *pktlog_ring;
	dhd_pktlog_filter_t *pktlog_filter;
	dll_t *item_p, *next_p;
	unsigned long flags = 0;

#ifdef BDC
	struct bdc_header *h;
	BCM_REFERENCE(h);
#endif /* BDC */
	/*
	 * dhdp, dhdp->pktlog, dhd->pktlog_ring, pktlog_ring->start
	 * are validated from the DHD_PKTLOG_TXS macro
	 */

	pktlog_ring = dhdp->pktlog->pktlog_ring;
	pktlog_filter = dhdp->pktlog->pktlog_filter;

	if (dhd_pktlog_filter_matched(pktlog_filter, pktdata,
		PKTLOG_TXSTATUS_CASE) == FALSE) {
		return BCME_OK;
	}

	pkt_hash = __dhd_dbg_pkt_hash((uintptr_t)pkt, pktid);
	pkt_fate = __dhd_dbg_map_tx_status_to_pkt_fate(status);

	/* find the sent tx packet and adding pkt_fate info */
	DHD_PKT_LOG_LOCK(pktlog_ring->pktlog_ring_lock, flags);
	/* Inverse traverse from the last packets */
	for (item_p = dll_tail_p(&pktlog_ring->ring_info_head);
		!dll_end(&pktlog_ring->ring_info_head, item_p);
		item_p = next_p)
	{
	    if (dll_empty(item_p)) {
		break;
	    }
	    next_p = dll_prev_p(item_p);
	    tx_pkt = (dhd_pktlog_ring_info_t *)item_p;
	    temp_hash = tx_pkt->info.pkt_hash;
	    if (temp_hash == pkt_hash) {
		tx_pkt->tx_fate = pkt_fate;
#ifdef BDC
		h = (struct bdc_header *)PKTDATA(dhdp->osh, tx_pkt->info.pkt);
		PKTPULL(dhdp->osh, tx_pkt->info.pkt, BDC_HEADER_LEN);
		PKTPULL(dhdp->osh, tx_pkt->info.pkt, (h->dataOffset << DHD_WORD_TO_LEN_SHIFT));
#endif /* BDC */
		DHD_PKT_LOG(("%s(): Found pkt hash in prev pos\n", __FUNCTION__));
		break;
	    }
	}
	DHD_PKT_LOG_UNLOCK(pktlog_ring->pktlog_ring_lock, flags);
	return BCME_OK;
}

dhd_pktlog_filter_t*
dhd_pktlog_filter_init(int size)
{
	int i;
	gfp_t kflags;
	uint32 alloc_len;
	dhd_pktlog_filter_t *filter;
	dhd_pktlog_filter_info_t *filter_info = NULL;

	kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;

	/* allocate and initialze pktmon filter */
	alloc_len = sizeof(dhd_pktlog_filter_t);
	filter = (dhd_pktlog_filter_t *)kzalloc(alloc_len, kflags);
	if (unlikely(!filter)) {
		DHD_ERROR(("%s(): could not allocate memory for - "
					"dhd_pktlog_filter_t\n", __FUNCTION__));
		goto fail;
	}

	alloc_len = (sizeof(dhd_pktlog_filter_info_t) * size);
	filter_info = (dhd_pktlog_filter_info_t *)kzalloc(alloc_len, kflags);
	if (unlikely(!filter_info)) {
		DHD_ERROR(("%s(): could not allocate memory for - "
					"dhd_pktlog_filter_info_t\n", __FUNCTION__));
		goto fail;
	}

	filter->info = filter_info;
	filter->list_cnt = 0;

	for (i = 0; i < MAX_DHD_PKTLOG_FILTER_LEN; i++) {
		filter->info[i].id = 0;
	}

	filter->enable = PKTLOG_TXPKT_CASE | PKTLOG_TXSTATUS_CASE | PKTLOG_RXPKT_CASE;

	DHD_ERROR(("%s(): pktlog filter init success\n", __FUNCTION__));

	return filter;
fail:
	if (filter) {
		kfree(filter);
	}

	return NULL;
}

int
dhd_pktlog_filter_deinit(dhd_pktlog_filter_t *filter)
{
	int ret = BCME_OK;

	if (!filter) {
		DHD_ERROR(("%s(): filter is NULL\n", __FUNCTION__));
		return -EINVAL;
	}

	if (filter->info) {
		kfree(filter->info);
	}
	kfree(filter);

	DHD_ERROR(("%s(): pktlog filter deinit\n", __FUNCTION__));

	return ret;
}

bool
dhd_pktlog_filter_existed(dhd_pktlog_filter_t *filter, char *arg, uint32 *id)
{
	char filter_pattern[MAX_FILTER_PATTERN_LEN];
	char *p;
	int i, j;
	int nchar;
	int len;

	if  (!filter || !arg) {
		DHD_ERROR(("%s(): filter=%p arg=%p\n", __FUNCTION__, filter, arg));
		return TRUE;
	}

	for (i = 0; i < filter->list_cnt; i++) {
		p = filter_pattern;
		len = sizeof(filter_pattern);

		nchar = snprintf(p, len, "%d ", filter->info[i].offset);
		p += nchar;
		len -= nchar;

		nchar = snprintf(p, len, "0x");
		p += nchar;
		len -= nchar;

		for (j = 0; j < filter->info[i].size_bytes; j++) {
			nchar = snprintf(p, len, "%02x", filter->info[i].mask[j]);
			p += nchar;
			len -= nchar;
		}

		nchar = snprintf(p, len, " 0x");
		p += nchar;
		len -= nchar;

		for (j = 0; j < filter->info[i].size_bytes; j++) {
			nchar = snprintf(p, len, "%02x", filter->info[i].pattern[j]);
			p += nchar;
			len -= nchar;
		}

		if (strlen(arg) < strlen(filter_pattern)) {
			continue;
		}

		DHD_PKT_LOG(("%s(): Pattern %s\n", __FUNCTION__, filter_pattern));

		if (strncmp(filter_pattern, arg, strlen(filter_pattern)) == 0) {
			*id = filter->info[i].id;
			DHD_ERROR(("%s(): This pattern is existed\n", __FUNCTION__));
			DHD_ERROR(("%s(): arg %s\n", __FUNCTION__, arg));
			return TRUE;
		}
	}

	return FALSE;
}

int
dhd_pktlog_filter_add(dhd_pktlog_filter_t *filter, char *arg)
{
	int32 mask_size, pattern_size;
	char *offset, *bitmask, *pattern;
	uint32 id = 0;

	if  (!filter || !arg) {
		DHD_ERROR(("%s(): pktlog_filter =%p arg =%p\n", __FUNCTION__, filter, arg));
		return BCME_ERROR;
	}

	DHD_PKT_LOG(("%s(): arg %s\n", __FUNCTION__, arg));

	if (dhd_pktlog_filter_existed(filter, arg, &id) == TRUE) {
		DHD_PKT_LOG(("%s(): This pattern id %d is existed\n", __FUNCTION__, id));
		return BCME_OK;
	}

	if (filter->list_cnt >= MAX_DHD_PKTLOG_FILTER_LEN) {
		DHD_ERROR(("%s(): pktlog filter full\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if ((offset = bcmstrtok(&arg, " ", 0)) == NULL) {
		DHD_ERROR(("%s(): offset not found\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if ((bitmask = bcmstrtok(&arg, " ", 0)) == NULL) {
		DHD_ERROR(("%s(): bitmask not found\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if ((pattern = bcmstrtok(&arg, " ", 0)) == NULL) {
		DHD_ERROR(("%s(): pattern not found\n", __FUNCTION__));
		return BCME_ERROR;
	}

	/* parse filter bitmask */
	mask_size = pattern_atoh_len(bitmask,
			(char *) &filter->info[filter->list_cnt].mask[0],
			MAX_MASK_PATTERN_FILTER_LEN);
	if (mask_size == -1) {
		DHD_ERROR(("Rejecting: %s\n", bitmask));
		return BCME_ERROR;
	}

	/* parse filter pattern */
	pattern_size = pattern_atoh_len(pattern,
			(char *) &filter->info[filter->list_cnt].pattern[0],
			MAX_MASK_PATTERN_FILTER_LEN);
	if (pattern_size == -1) {
		DHD_ERROR(("Rejecting: %s\n", pattern));
		return BCME_ERROR;
	}

	prhex("mask", (char *)&filter->info[filter->list_cnt].mask[0],
			mask_size);
	prhex("pattern", (char *)&filter->info[filter->list_cnt].pattern[0],
			pattern_size);

	if (mask_size != pattern_size) {
		DHD_ERROR(("%s(): Mask and pattern not the same size\n", __FUNCTION__));
		return BCME_ERROR;
	}

	filter->info[filter->list_cnt].offset = strtoul(offset, NULL, 0);
	filter->info[filter->list_cnt].size_bytes = mask_size;
	filter->info[filter->list_cnt].id = filter->list_cnt + 1;
	filter->info[filter->list_cnt].enable = TRUE;

	filter->list_cnt++;

	return BCME_OK;
}

int
dhd_pktlog_filter_del(dhd_pktlog_filter_t *filter, char *arg)
{
	uint32 id = 0;

	if  (!filter || !arg) {
		DHD_ERROR(("%s(): pktlog_filter =%p arg =%p\n", __FUNCTION__, filter, arg));
		return BCME_ERROR;
	}

	DHD_PKT_LOG(("%s(): arg %s\n", __FUNCTION__, arg));

	if (dhd_pktlog_filter_existed(filter, arg, &id) != TRUE) {
		DHD_PKT_LOG(("%s(): This pattern id %d doesn't existed\n", __FUNCTION__, id));
		return BCME_OK;
	}

	dhd_pktlog_filter_pull_forward(filter, id, filter->list_cnt);

	filter->list_cnt--;

	return BCME_OK;
}

int
dhd_pktlog_filter_enable(dhd_pktlog_filter_t *filter, uint32 pktmon_case, uint32 enable)
{
	if  (!filter) {
		DHD_ERROR(("%s(): filter is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	DHD_PKT_LOG(("%s(): pktlog_case %d enable %d\n", __FUNCTION__, pktmon_case, enable));

	if (enable) {
		filter->enable |=  pktmon_case;
	} else {
		filter->enable &= ~pktmon_case;
	}

	return BCME_OK;
}

int
dhd_pktlog_filter_pattern_enable(dhd_pktlog_filter_t *filter, char *arg, uint32 enable)
{
	uint32 id = 0;

	if  (!filter || !arg) {
		DHD_ERROR(("%s(): pktlog_filter =%p arg =%p\n", __FUNCTION__, filter, arg));
		return BCME_ERROR;
	}

	if (dhd_pktlog_filter_existed(filter, arg, &id) == TRUE) {
		if (id > 0) {
			filter->info[id-1].enable = enable;
			DHD_ERROR(("%s(): This pattern id %d is %s\n",
				__FUNCTION__, id, (enable ? "enabled" : "disabled")));
		}
	} else {
		DHD_ERROR(("%s(): This pattern is not existed\n", __FUNCTION__));
		DHD_ERROR(("%s(): arg %s\n", __FUNCTION__, arg));
	}

	return BCME_OK;
}

int
dhd_pktlog_filter_info(dhd_pktlog_filter_t *filter)
{
	char filter_pattern[MAX_FILTER_PATTERN_LEN];
	char *p;
	int i, j;
	int nchar;
	int len;

	if  (!filter) {
		DHD_ERROR(("%s(): pktlog_filter is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	DHD_ERROR(("---- PKTLOG FILTER INFO ----\n\n"));

	DHD_ERROR(("Filter list cnt %d Filter is %s\n",
		filter->list_cnt, (filter->enable ? "enabled" : "disabled")));

	for (i = 0; i < filter->list_cnt; i++) {
		p = filter_pattern;
		len = sizeof(filter_pattern);

		nchar = snprintf(p, len, "%d ", filter->info[i].offset);
		p += nchar;
		len -= nchar;

		nchar = snprintf(p, len, "0x");
		p += nchar;
		len -= nchar;

		for (j = 0; j < filter->info[i].size_bytes; j++) {
			nchar = snprintf(p, len, "%02x", filter->info[i].mask[j]);
			p += nchar;
			len -= nchar;
		}

		nchar = snprintf(p, len, " 0x");
		p += nchar;
		len -= nchar;

		for (j = 0; j < filter->info[i].size_bytes; j++) {
			nchar = snprintf(p, len, "%02x", filter->info[i].pattern[j]);
			p += nchar;
			len -= nchar;
		}

		DHD_ERROR(("ID:%d is %s\n",
			filter->info[i].id, (filter->info[i].enable ? "enabled" : "disabled")));
		DHD_ERROR(("Pattern %s\n", filter_pattern));
	}

	DHD_ERROR(("---- PKTLOG FILTER END ----\n"));

	return BCME_OK;
}
bool
dhd_pktlog_filter_matched(dhd_pktlog_filter_t *filter, char *data, uint32 pktlog_case)
{
	uint16 szbts;	/* pattern size */
	uint16 offset;	/* pattern offset */
	int i, j;
	uint8 *mask = NULL;		/* bitmask */
	uint8 *pattern = NULL;
	uint8 *pkt_offset = NULL;	/* packet offset */
	bool matched;

	if  (!filter || !data) {
		DHD_PKT_LOG(("%s(): filter=%p data=%p\n",
			__FUNCTION__, filter, data));
		return TRUE;
	}

	if (!(pktlog_case & filter->enable)) {
		DHD_PKT_LOG(("%s(): pktlog_case %d return TRUE filter is disabled\n",
			__FUNCTION__, pktlog_case));
		return TRUE;
	}

	for (i = 0; i < filter->list_cnt; i++) {
		if (&filter->info[i] && filter->info[i].id && filter->info[i].enable) {
			szbts = filter->info[i].size_bytes;
			offset = filter->info[i].offset;
			mask = &filter->info[i].mask[0];
			pkt_offset = &data[offset];
			pattern = &filter->info[i].pattern[0];

			matched = TRUE;
			for (j = 0; j < szbts; j++) {
				if ((mask[j] & pkt_offset[j]) != pattern[j]) {
					matched = FALSE;
					break;
				}
			}

			if (matched) {
				DHD_PKT_LOG(("%s(): pktlog_filter return TRUE id %d\n",
					__FUNCTION__, filter->info[i].id));
				return TRUE;
			}
		} else {
			DHD_PKT_LOG(("%s(): filter ino is null %p\n",
				__FUNCTION__, &filter->info[i]));
		}
	}

	return FALSE;
}

/* Ethernet Type MAC Header 12 bytes + Frame payload 10 bytes */
#define PKTLOG_MINIMIZE_REPORT_LEN 22

static char pktlog_minmize_mask_table[] = {
	0xff, 0x00, 0x00, 0x00, 0xff, 0x0f, /* Ethernet Type MAC Header - Destination MAC Address */
	0xff, 0x00, 0x00, 0x00, 0xff, 0x0f, /* Ethernet Type MAC Header - Source MAC Address */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* Ethernet Type MAC Header - Ether Type - 2 bytes */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* Frame payload */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xff, 0xff, /* UDP port number offset - bytes as 0xff */
	0xff, 0xff,
};

static inline void
dhd_pktlog_minimize_report(char *pkt, uint32 frame_len,
		void *file, const void *user_buf, void *pos)
{
	int i;
	int ret = 0;
	int table_len;
	int report_len;
	char *p_table;
	char *mem_buf = NULL;

	table_len = sizeof(pktlog_minmize_mask_table);
	report_len =  table_len;
	p_table = &pktlog_minmize_mask_table[0];

	if (frame_len < PKTLOG_MINIMIZE_REPORT_LEN) {
		DHD_ERROR(("%s : frame_len is samller than min\n", __FUNCTION__));
		return;
	}

	mem_buf = vmalloc(frame_len);
	if (!mem_buf) {
		DHD_ERROR(("%s : failed to alloc membuf\n", __FUNCTION__));
		return;
	}

	bzero(mem_buf, frame_len);

	if (frame_len < table_len) {
		report_len = PKTLOG_MINIMIZE_REPORT_LEN;
	}

	for (i = 0; i < report_len; i++) {
		mem_buf[i] = pkt[i] & p_table[i];
	}

	ret = dhd_export_debug_data(mem_buf,
			file, user_buf, frame_len, pos);
	if (ret < 0) {
		DHD_ERROR(("%s : Write minimize report\n", __FUNCTION__));
	}
	vfree(mem_buf);
}

dhd_pktlog_ring_t*
dhd_pktlog_ring_change_size(dhd_pktlog_ring_t *ringbuf, int size)
{
	uint32 alloc_len;
	uint32 pktlog_minmize;
	dhd_pktlog_ring_t *pktlog_ring = NULL;
	dhd_pub_t *dhdp;

	if  (!ringbuf) {
		DHD_ERROR(("%s(): ringbuf is NULL\n", __FUNCTION__));
		return NULL;
	}

	alloc_len = size;
	if (alloc_len < MIN_PKTLOG_LEN) {
		alloc_len = MIN_PKTLOG_LEN;
	}
	if (alloc_len > MAX_PKTLOG_LEN) {
		alloc_len = MAX_PKTLOG_LEN;
	}
	DHD_ERROR(("ring size requested: %d alloc: %d\n", size, alloc_len));

	/* backup variable */
	pktlog_minmize = ringbuf->pktlog_minmize;
	dhdp = ringbuf->dhdp;

	/* free ring_info */
	dhd_pktlog_ring_deinit(dhdp, ringbuf);

	/* alloc ring_info */
	pktlog_ring = dhd_pktlog_ring_init(dhdp, alloc_len);

	/* restore variable */
	if (pktlog_ring) {
		OSL_ATOMIC_SET(dhdp->osh, &pktlog_ring->start, TRUE);
		pktlog_ring->pktlog_minmize = pktlog_minmize;
	}

	return pktlog_ring;
}

void
dhd_pktlog_filter_pull_forward(dhd_pktlog_filter_t *filter, uint32 del_filter_id, uint32 list_cnt)
{
	int ret = 0;
	int pos = 0;
	int move_list_cnt = 0;
	int move_bytes = 0;

	if ((del_filter_id > list_cnt) ||
		(list_cnt > MAX_DHD_PKTLOG_FILTER_LEN)) {
		DHD_ERROR(("Wrong id %d cnt %d tried to remove\n", del_filter_id, list_cnt));
		return;
	}

	move_list_cnt = list_cnt - del_filter_id;

	pos = del_filter_id -1;
	move_bytes = sizeof(dhd_pktlog_filter_info_t) * move_list_cnt;
	if (move_list_cnt) {
		ret = memmove_s(&filter->info[pos], move_bytes + sizeof(dhd_pktlog_filter_info_t),
				&filter->info[pos+1], move_bytes);
		if (ret) {
			DHD_ERROR(("filter moving failed\n"));
			return;
		}
		for (; pos < list_cnt -1; pos++) {
			filter->info[pos].id -= 1;
		}
	}
	bzero(&filter->info[list_cnt-1], sizeof(dhd_pktlog_filter_info_t));
}

void dhd_pktlog_get_filename(dhd_pub_t *dhdp, char *dump_path, int len)
{
	/* Init file name */
	bzero(dump_path, len);
	clear_debug_dump_time(dhdp->debug_dump_time_pktlog_str);
	get_debug_dump_time(dhdp->debug_dump_time_pktlog_str);

	if (dhdp->memdump_type == DUMP_TYPE_BY_SYSDUMP) {
		if (dhdp->debug_dump_subcmd == CMD_UNWANTED) {
			snprintf(dump_path, len, "%s",
					DHD_PKTLOG_DUMP_PATH DHD_PKTLOG_DUMP_TYPE
					DHD_DUMP_SUBSTR_UNWANTED);
		} else if (dhdp->debug_dump_subcmd == CMD_DISCONNECTED) {
			snprintf(dump_path, len, "%s",
					DHD_PKTLOG_DUMP_PATH DHD_PKTLOG_DUMP_TYPE
					DHD_DUMP_SUBSTR_DISCONNECTED);
		} else {
			snprintf(dump_path, len, "%s",
					DHD_PKTLOG_DUMP_PATH DHD_PKTLOG_DUMP_TYPE);
		}
	} else {
		if (dhdp->pktlog_debug) {
			snprintf(dump_path, len, "%s",
					DHD_PKTLOG_DUMP_PATH DHD_PKTLOG_DEBUG_DUMP_TYPE);
		} else {
			snprintf(dump_path, len, "%s",
					DHD_PKTLOG_DUMP_PATH DHD_PKTLOG_DUMP_TYPE);
		}

	}

	snprintf(dump_path, len, "%s_%s.pcap", dump_path,
			dhdp->debug_dump_time_pktlog_str);
	DHD_ERROR(("%s: pktlog path = %s%s\n", __FUNCTION__, dump_path, FILE_NAME_HAL_TAG));
	clear_debug_dump_time(dhdp->debug_dump_time_pktlog_str);
}

uint32
dhd_pktlog_get_item_length(dhd_pktlog_ring_info_t *report_ptr)
{
	uint32 len = 0;
	char buf[DHD_PKTLOG_FATE_INFO_STR_LEN];
	int bytes_user_data = 0;
	uint32 write_frame_len;
	uint32 frame_len;

	len += (uint32)sizeof(report_ptr->info.driver_ts_sec);
	len += (uint32)sizeof(report_ptr->info.driver_ts_usec);

	if (report_ptr->info.payload_type == FRAME_TYPE_ETHERNET_II) {
		frame_len = (uint32)min(report_ptr->info.pkt_len, (size_t)MAX_FRAME_LEN_ETHERNET);
	} else {
		frame_len = (uint32)min(report_ptr->info.pkt_len, (size_t)MAX_FRAME_LEN_80211_MGMT);
	}

	bytes_user_data = sprintf(buf, "%s:%s:%02d\n", DHD_PKTLOG_FATE_INFO_FORMAT,
			(report_ptr->tx_fate ? "Failure" : "Succeed"), report_ptr->tx_fate);
	write_frame_len = frame_len + bytes_user_data;

	/* pcap pkt head has incl_len and orig_len */
	len += (uint32)sizeof(write_frame_len);
	len += (uint32)sizeof(write_frame_len);
	len += frame_len;
	len += bytes_user_data;

	return len;
}

uint32
dhd_pktlog_get_dump_length(dhd_pub_t *dhdp)
{
	dhd_pktlog_ring_info_t *report_ptr;
	dhd_pktlog_ring_t *pktlog_ring;
	uint32 len;
	dll_t *item_p, *next_p;

	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	if (!dhdp->pktlog->pktlog_ring) {
		DHD_PKT_LOG(("%s(): pktlog_ring =%p\n",
			__FUNCTION__, dhdp->pktlog->pktlog_ring));
		return -EINVAL;
	}

	pktlog_ring = dhdp->pktlog->pktlog_ring;
	OSL_ATOMIC_SET(dhdp->osh, &pktlog_ring->start, FALSE);

	len = sizeof(dhd_pktlog_pcap_hdr_t);

	for (item_p = dll_head_p(&pktlog_ring->ring_info_head);
			!dll_end(&pktlog_ring->ring_info_head, item_p);
			item_p = next_p) {
		next_p = dll_next_p(item_p);
		report_ptr = (dhd_pktlog_ring_info_t *)item_p;
		len += dhd_pktlog_get_item_length(report_ptr);
	}
	OSL_ATOMIC_SET(dhdp->osh, &pktlog_ring->start, TRUE);
	DHD_PKT_LOG(("calcuated pkt log dump len:%d\n", len));

	return len;
}

int
dhd_pktlog_dump_write(dhd_pub_t *dhdp, void *file, const void *user_buf, uint32 size)
{
	dhd_pktlog_ring_info_t *report_ptr;
	dhd_pktlog_ring_t *pktlog_ring;
	char buf[DHD_PKTLOG_FATE_INFO_STR_LEN];
	dhd_pktlog_pcap_hdr_t pcap_h;
	uint32 write_frame_len;
	uint32 frame_len;
	ulong len;
	int bytes_user_data = 0;
	loff_t pos = 0;
	int ret = BCME_OK;
	dll_t *item_p, *next_p;

	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	if (!dhdp->pktlog->pktlog_ring) {
		DHD_PKT_LOG(("%s(): pktlog_ring =%p\n",
			__FUNCTION__, dhdp->pktlog->pktlog_ring));
		return -EINVAL;
	}

	if (file && !user_buf && (size == 0)) {
		DHD_ERROR(("Local file pktlog dump requested\n"));
	} else if (!file && user_buf && (size > 0)) {
		DHD_ERROR(("HAL file pktlog dump %d bytes requested\n", size));
	} else {
		DHD_ERROR(("Wrong type pktlog dump requested\n"));
		return -EINVAL;
	}

	pktlog_ring = dhdp->pktlog->pktlog_ring;
	OSL_ATOMIC_SET(dhdp->osh, &pktlog_ring->start, FALSE);

	pcap_h.magic_number = PKTLOG_PCAP_MAGIC_NUM;
	pcap_h.version_major = PKTLOG_PCAP_MAJOR_VER;
	pcap_h.version_minor = PKTLOG_PCAP_MINOR_VER;
	pcap_h.thiszone = 0x0;
	pcap_h.sigfigs = 0x0;
	pcap_h.snaplen = PKTLOG_PCAP_SNAP_LEN;
	pcap_h.network = PKTLOG_PCAP_NETWORK_TYPE;

	ret = dhd_export_debug_data((char *)&pcap_h, file, user_buf, sizeof(pcap_h), &pos);
	len = sizeof(pcap_h);

	for (item_p = dll_head_p(&pktlog_ring->ring_info_head);
			!dll_end(&pktlog_ring->ring_info_head, item_p);
			item_p = next_p) {

		next_p = dll_next_p(item_p);
		report_ptr = (dhd_pktlog_ring_info_t *)item_p;

		if ((file == NULL) &&
			(len + dhd_pktlog_get_item_length(report_ptr) > size)) {
			DHD_ERROR(("overflowed pkt logs are dropped\n"));
			break;
		}

		ret = dhd_export_debug_data((char*)&report_ptr->info.driver_ts_sec, file,
				user_buf, sizeof(report_ptr->info.driver_ts_sec), &pos);
		len += sizeof(report_ptr->info.driver_ts_sec);

		ret = dhd_export_debug_data((char*)&report_ptr->info.driver_ts_usec, file,
				user_buf, sizeof(report_ptr->info.driver_ts_usec), &pos);
		len += sizeof(report_ptr->info.driver_ts_usec);

		if (report_ptr->info.payload_type == FRAME_TYPE_ETHERNET_II) {
			frame_len = (uint32)min(report_ptr->info.pkt_len,
					(size_t)MAX_FRAME_LEN_ETHERNET);

		} else {
			frame_len = (uint32)min(report_ptr->info.pkt_len,
					(size_t)MAX_FRAME_LEN_80211_MGMT);
		}

		bytes_user_data = sprintf(buf, "%s:%s:%02d\n", DHD_PKTLOG_FATE_INFO_FORMAT,
				(report_ptr->tx_fate ? "Failure" : "Succeed"), report_ptr->tx_fate);
		write_frame_len = frame_len + bytes_user_data;

		/* pcap pkt head has incl_len and orig_len */
		ret = dhd_export_debug_data((char*)&write_frame_len, file, user_buf,
				sizeof(write_frame_len), &pos);
		len += sizeof(write_frame_len);

		ret = dhd_export_debug_data((char*)&write_frame_len, file, user_buf,
				sizeof(write_frame_len), &pos);
		len += sizeof(write_frame_len);

		if (pktlog_ring->pktlog_minmize) {
			dhd_pktlog_minimize_report(PKTDATA(pktlog_ring->dhdp->osh,
					report_ptr->info.pkt), frame_len, file, user_buf, &pos);
		} else {
			ret = dhd_export_debug_data(PKTDATA(pktlog_ring->dhdp->osh,
					report_ptr->info.pkt), file, user_buf, frame_len, &pos);
		}
		len += frame_len;

		ret = dhd_export_debug_data(buf, file, user_buf, bytes_user_data, &pos);
		len += bytes_user_data;
	}
	OSL_ATOMIC_SET(dhdp->osh, &pktlog_ring->start, TRUE);

	return ret;
}

int
dhd_pktlog_dump_write_memory(dhd_pub_t *dhdp, const void *user_buf, uint32 size)
{
	int ret = dhd_pktlog_dump_write(dhdp, NULL, user_buf, size);
	if (ret < 0) {
		DHD_ERROR(("dhd_pktlog_dump_write_memory error\n"));
	}
	return ret;
}

int
dhd_pktlog_dump_write_file(dhd_pub_t *dhdp)
{
	struct file *w_pcap_fp = NULL;
	uint32 file_mode;
	mm_segment_t old_fs;
	char pktlogdump_path[128];
	int ret = BCME_OK;

	dhd_pktlog_get_filename(dhdp, pktlogdump_path, 128);
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	file_mode = O_CREAT | O_WRONLY;

	w_pcap_fp = filp_open(pktlogdump_path, file_mode, 0664);
	if (IS_ERR(w_pcap_fp)) {
		DHD_ERROR(("%s: Couldn't open file '%s' err %ld\n",
			__FUNCTION__, pktlogdump_path, PTR_ERR(w_pcap_fp)));
		ret = BCME_ERROR;
		goto fail;
	}

	dhd_pktlog_dump_write(dhdp, w_pcap_fp, NULL, 0);
	if (ret < 0) {
		DHD_ERROR(("dhd_pktlog_dump_write error\n"));
		goto fail;
	}

	/* Sync file from filesystem to physical media */
	ret = vfs_fsync(w_pcap_fp, 0);
	if (ret < 0) {
		DHD_ERROR(("%s(): sync pcap file error, err = %d\n", __FUNCTION__, ret));
		goto fail;
	}
fail:
	if (!IS_ERR(w_pcap_fp)) {
		filp_close(w_pcap_fp, NULL);
	}

	set_fs(old_fs);

#ifdef DHD_DUMP_MNGR
	if (ret >= 0) {
		dhd_dump_file_manage_enqueue(dhdp, pktlogdump_path, DHD_PKTLOG_DUMP_TYPE);
	}
#endif /* DHD_DUMP_MNGR */
	return ret;
}

#ifdef DHD_COMPACT_PKT_LOG
static uint64
dhd_cpkt_log_calc_time_diff(dhd_pktlog_ring_info_t *pkt_info, uint64 curr_ts_nsec)
{
	uint64 pkt_ts_nsec = pkt_info->info.driver_ts_sec * NSEC_PER_SEC +
		pkt_info->info.driver_ts_usec * NSEC_PER_USEC;

	return (curr_ts_nsec - pkt_ts_nsec) / NSEC_PER_USEC;
}

static int
dhd_cpkt_log_get_ts_idx(dhd_pktlog_t *pktlog, dhd_pktlog_ring_info_t *pkt_info, u64 curr_ts_nsec)
{
	struct rb_node *n = pktlog->cpkt_log_tt_rbt.rb_node;
	dhd_cpkt_log_ts_node_t *node = NULL;

	uint64 ts_diff = dhd_cpkt_log_calc_time_diff(pkt_info, curr_ts_nsec);

	if (ts_diff > dhd_cpkt_log_tt_idx[CPKT_LOG_TT_IDX_ARR_SZ - 1])
		return CPKT_LOG_TT_IDX_ARR_SZ;

	while (n) {
		node = rb_entry(n, dhd_cpkt_log_ts_node_t, rb);

		if (ts_diff < node->ts_diff)
			n = n->rb_left;
		else if (ts_diff > node->ts_diff)
			n = n->rb_right;
		else
			break;
	}

	if (node != NULL) {
		if (node->idx && ts_diff < node->ts_diff)
			return node->idx - 1;
		return node->idx;
	}

	return BCME_NOTFOUND;
}

static int
dhd_cpkt_log_get_direction(dhd_pktlog_ring_info_t *pkt_info)
{
	return pkt_info->info.direction == PKTLOG_TXPKT_CASE ? PKT_TX : PKT_RX;
}

static int
dhd_cpkt_log_get_802_1x_subtype(eapol_header_t *eapol)
{
	int subtype;
	eap_header_t *eap;
	eapol_wpa_key_header_t *ek;

	uint16 key_info;
	int pair, ack, mic, kerr, req, sec, install;

	subtype = CPKT_LOG_802_1X_SUBTYPE_OTHERS;
	if (eapol->type != EAPOL_KEY) {
		eap = (eap_header_t *)eapol->body;

		switch (eap->type) {
		case EAP_IDENTITY:
			subtype = CPKT_LOG_802_1X_SUBTYPE_IDENTITY;
			break;
		case REALM_EAP_TLS:
			subtype = CPKT_LOG_802_1X_SUBTYPE_TLS;
			break;
		case REALM_EAP_TTLS:
			subtype = CPKT_LOG_802_1X_SUBTYPE_TTLS;
			break;
		case REALM_EAP_FAST:
			subtype = CPKT_LOG_802_1X_SUBTYPE_FAST;
			break;
		case REALM_EAP_LEAP:
			subtype = CPKT_LOG_802_1X_SUBTYPE_LEAP;
			break;
		case REALM_EAP_PSK:
			subtype = CPKT_LOG_802_1X_SUBTYPE_PWD;
			break;
		case REALM_EAP_SIM:
			subtype = CPKT_LOG_802_1X_SUBTYPE_SIM;
			break;
		case REALM_EAP_AKA:
			subtype = CPKT_LOG_802_1X_SUBTYPE_AKA;
			break;
		case REALM_EAP_AKAP:
			subtype = CPKT_LOG_802_1X_SUBTYPE_AKAP;
			break;
		default:
			break;
		}
		if (eap->code == EAP_SUCCESS)
			subtype = CPKT_LOG_802_1X_SUBTYPE_SUCCESS;
	} else {
		/* in case of 4 way handshake */
		ek = (eapol_wpa_key_header_t *)(eapol->body);

		if (ek->type == EAPOL_WPA2_KEY || ek->type == EAPOL_WPA_KEY) {
			key_info = ntoh16_ua(&ek->key_info);

			pair =  0 != (key_info & WPA_KEY_PAIRWISE);
			ack = 0  != (key_info & WPA_KEY_ACK);
			mic = 0  != (key_info & WPA_KEY_MIC);
			kerr =  0 != (key_info & WPA_KEY_ERROR);
			req = 0  != (key_info & WPA_KEY_REQ);
			sec = 0  != (key_info & WPA_KEY_SECURE);
			install  = 0 != (key_info & WPA_KEY_INSTALL);

			if (!sec && !mic && ack && !install && pair && !kerr && !req)
				subtype = CPKT_LOG_802_1X_SUBTYPE_4WAY_M1;
			else if (pair && !install && !ack && mic && !sec && !kerr && !req)
				subtype = CPKT_LOG_802_1X_SUBTYPE_4WAY_M2;
			else if (pair && ack && mic && sec && !kerr && !req)
				subtype = CPKT_LOG_802_1X_SUBTYPE_4WAY_M3;
			else if (pair && !install && !ack && mic && sec && !req && !kerr)
				subtype = CPKT_LOG_802_1X_SUBTYPE_4WAY_M4;
		}
	}

	return subtype;
}

static int
dhd_cpkt_log_get_pkt_info(dhd_pktlog_t *pktlog, dhd_pktlog_ring_info_t *pkt_info)
{
	int type;
	int subtype = 0;

	uint8 prot;
	uint16 src_port, dst_port;
	int len, offset;

	uint8 *pdata;
	uint8 *pkt_data;

	uint16 eth_type;
	struct bcmarp *arp;
	struct bcmicmp_hdr *icmp;
	struct ipv4_hdr *ipv4;
	struct ether_header *eth_hdr;
	bcm_tlv_t *dhcp_opt;

	struct ipv6_hdr *ipv6;
	struct icmp6_hdr *icmpv6_hdr;

	pkt_data = (uint8 *)PKTDATA(pktlog->dhdp->osh, pkt_info->info.pkt);

	eth_hdr = (struct ether_header *)pkt_data;
	eth_type = ntoh16(eth_hdr->ether_type);

	type = CPKT_LOG_TYPE_OTHERS;
	switch (eth_type) {
	case ETHER_TYPE_IP:
		if (get_pkt_ip_type(pktlog->dhdp->osh, pkt_info->info.pkt,
			&pdata, &len, &prot) != 0) {
			DHD_PKT_LOG(("%s: fail to get pkt ip type\n", __FUNCTION__));
			return BCME_ERROR;
		}

		if (prot == IP_PROT_ICMP) {
			icmp = (struct bcmicmp_hdr *)(pdata);
			if (!(icmp->type == ICMP_TYPE_ECHO_REQUEST ||
				icmp->type == ICMP_TYPE_ECHO_REPLY ||
				icmp->type == CPKT_LOG_ICMP_TYPE_DEST_UNREACHABLE)) {
				return BCME_ERROR;
			}

			if (icmp->type == ICMP_TYPE_ECHO_REQUEST) {
				type = CPKT_LOG_TYPE_ICMP_REQ;
				/* Subtype = Last 8 bits of identifier */
				subtype = ntoh16_ua(pdata + sizeof(*icmp)) & 0xFF;
			} else if (icmp->type == ICMP_TYPE_ECHO_REPLY) {
				type = CPKT_LOG_TYPE_ICMP_RES;
				/* Subtype = Last 8 bits of identifier */
				subtype = ntoh16_ua(pdata + sizeof(*icmp)) & 0xFF;
			} else if (icmp->type == CPKT_LOG_ICMP_TYPE_DEST_UNREACHABLE) {
				type = CPKT_LOG_TYPE_ICMP_UNREACHABLE;
				/* Subtype = Last 8 bits of identifier */
				ipv4 = (struct ipv4_hdr *)(pdata + sizeof(*icmp) +
						CPKT_LOG_ICMP_TYPE_DEST_UNREACHABLE_IPV4_OFFSET);
				subtype = ipv4->id & 0xFF;
			}

			DHD_PKT_LOG(("%s: type = ICMP(%d), subtype = %x \n",
				__FUNCTION__, type, subtype));
		} else if (prot == IP_PROT_UDP) {
			if (len < UDP_HDR_LEN)
				return BCME_ERROR;

			src_port = ntoh16_ua(pdata);
			dst_port = ntoh16_ua(pdata + UDP_DEST_PORT_OFFSET);

			if (src_port == DHCP_PORT_SERVER || src_port == DHCP_PORT_CLIENT) {
				type = CPKT_LOG_TYPE_DHCP;
				/* Subtype = DHCP message type */
				offset = DHCP_OPT_OFFSET + CPKT_LOG_DHCP_MAGIC_COOKIE_LEN;
				if ((UDP_HDR_LEN + offset) >= len)
					return BCME_ERROR;
				len -= (UDP_HDR_LEN - offset);

				dhcp_opt = bcm_parse_tlvs(pdata + UDP_HDR_LEN + offset,
					len, DHCP_OPT_MSGTYPE);
				if (dhcp_opt == NULL)
					return BCME_NOTFOUND;
				subtype = dhcp_opt->data[0];

				DHD_PKT_LOG(("%s: type = DHCP(%d), subtype = %x \n",
					__FUNCTION__, type, subtype));
			} else if (src_port == CPKT_LOG_DNS_PORT_CLIENT ||
				dst_port == CPKT_LOG_DNS_PORT_CLIENT ||
				dst_port == CPKT_LOG_MDNS_PORT_CLIENT) {
				type = CPKT_LOG_TYPE_DNS;
				/* Subtype = Last 8 bits of DNS Transaction ID */
				subtype = ntoh16_ua(pdata + UDP_HDR_LEN) & 0xFF;

				DHD_PKT_LOG(("%s: type = DNS(%d), subtype = %x \n",
					__FUNCTION__, type, subtype));
			} else {
				DHD_PKT_LOG(("%s: unsupported ports num (src:%d, dst:%d)\n",
					__FUNCTION__, src_port, dst_port));
			}
		} else {
			DHD_PKT_LOG(("%s: prot = %x\n", __FUNCTION__, prot));
		}

		break;
	case ETHER_TYPE_ARP:
		type = CPKT_LOG_TYPE_ARP;
		/* Subtype = Last 8 bits of target IP address */
		arp = (struct bcmarp *)(pkt_data + ETHER_HDR_LEN);
		subtype = arp->dst_ip[IPV4_ADDR_LEN - 1];

		DHD_PKT_LOG(("%s: type = ARP(%d), subtype = %x\n",
			__FUNCTION__, type, subtype));

		break;
	case ETHER_TYPE_802_1X:
		type = CPKT_LOG_TYPE_802_1X;
		/* EAPOL for 802.3/Ethernet */
		subtype = dhd_cpkt_log_get_802_1x_subtype((eapol_header_t *)pkt_data);

		DHD_PKT_LOG(("%s: type = 802.1x(%d), subtype = %x\n",
			__FUNCTION__, type, subtype));

		break;
	case ETHER_TYPE_IPV6:
		ipv6 = (struct ipv6_hdr *)(pkt_data + ETHER_HDR_LEN);
		if (ipv6->nexthdr == ICMPV6_HEADER_TYPE) {
			type = CPKT_LOG_TYPE_ICMPv6;
			icmpv6_hdr =
			       (struct icmp6_hdr *)(pkt_data + ETHER_HDR_LEN + sizeof(*ipv6));
			subtype = icmpv6_hdr->icmp6_type;

			DHD_PKT_LOG(("%s: type = ICMPv6(%x), subtype = %x\n",
				__FUNCTION__, type, subtype));
		} else {
			DHD_ERROR(("%s: unsupported ipv6 next header\n", __FUNCTION__));
		}

		break;
	default:
		DHD_ERROR(("%s: Invalid eth type (%x)\n", __FUNCTION__, eth_hdr->ether_type));
		break;
	}

	return (subtype << CPKT_LOG_BIT_LEN_TYPE) | type;
}

static int
dhd_cpkt_log_get_pkt_fate(dhd_pktlog_ring_info_t *pktlog_info)
{
	return pktlog_info->fate;
}

/*
 * dhd_cpkt_log_build: prepare 22 bits of data as compact packet log format to report to big data
 *
 * pkt_info: one packet data from packet log
 * curr_ts_nsec: current time (nano seconds)
 * cpkt: pointer for output(22 bits compact packet log)
 *
 */
static int
dhd_cpkt_log_build(dhd_pktlog_t *pktlog, dhd_pktlog_ring_info_t *pkt_info,
	u64 curr_ts_nsec, int *cpkt)
{
	int ret;
	int mask;
	int temp = 0;

	/* Timestamp index */
	ret = dhd_cpkt_log_get_ts_idx(pktlog, pkt_info, curr_ts_nsec);
	if (ret < 0) {
		DHD_ERROR(("%s: Invalid cpktlog ts, err = %d\n", __FUNCTION__, ret));
		return ret;
	}
	mask = CPKT_LOG_BIT_MASK_TS;
	temp |= ((ret & mask) << CPKT_LOG_BIT_OFFSET_TS);

	/* Direction: Tx/Rx */
	ret = dhd_cpkt_log_get_direction(pkt_info);
	mask = CPKT_LOG_BIT_MASK_DIR;
	temp |= ((ret & mask) << CPKT_LOG_BIT_OFFSET_DIR);

	/* Info = Packet Type & Packet Subtype */
	ret = dhd_cpkt_log_get_pkt_info(pktlog, pkt_info);
	if (ret < 0) {
		DHD_ERROR(("%s: Invalid cpktlog info, err = %d\n", __FUNCTION__, ret));
		return ret;
	}
	mask = CPKT_LOG_BIT_MASK_SUBTYPE << CPKT_LOG_BIT_LEN_TYPE | CPKT_LOG_BIT_MASK_TYPE;
	temp |= ((ret & mask) << CPKT_LOG_BIT_OFFSET_TYPE);

	/* Packet Fate */
	ret = dhd_cpkt_log_get_pkt_fate(pkt_info);
	mask = CPKT_LOG_BIT_MASK_PKT_FATE;
	temp |= ((ret & mask) << CPKT_LOG_BIT_OFFSET_PKT_FATE);

	*cpkt = temp;

	return BCME_OK;
}

int
dhd_cpkt_log_proc(dhd_pub_t *dhdp, char *buf, int buf_len, int bit_offset, int req_pkt_num)
{
	int ret;
	int cpkt;
	int offset = bit_offset;
	dll_t *item_p, *prev_p;

	uint8 pkt_cnt;
	u64 curr_ts_nsec;

	dhd_pktlog_t *pktlog;
	dhd_pktlog_ring_t *pktlog_rbuf;

	if (!dhdp || !dhdp->pktlog) {
		DHD_ERROR(("%s: dhdp or pktlog is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (!dhdp->pktlog->pktlog_ring) {
		DHD_ERROR(("%s: pktlog_ring is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	DHD_PKT_LOG(("%s: start cpkt log\n", __FUNCTION__));

	pktlog = dhdp->pktlog;
	pktlog_rbuf = pktlog->pktlog_ring;

	req_pkt_num = req_pkt_num > CPKT_LOG_MAX_NUM ?
		CPKT_LOG_MAX_NUM : req_pkt_num;

	pkt_cnt = 0;
	curr_ts_nsec = local_clock();
	for (item_p = dll_tail_p(&pktlog_rbuf->ring_info_head);
		!dll_end(&pktlog_rbuf->ring_info_head, item_p);
		item_p = prev_p) {
		prev_p = dll_prev_p(item_p);
		if (prev_p == NULL)
			break;

		ret = dhd_cpkt_log_build(pktlog, (dhd_pktlog_ring_info_t *)item_p,
			curr_ts_nsec, &cpkt);
		if (ret < 0)
			continue;

		offset = dhd_bit_pack(buf, buf_len, offset, cpkt, CPKT_LOG_BIT_SIZE);

		pkt_cnt++;
		if (pkt_cnt >= req_pkt_num)
			break;
	}

	return offset;
}

static void
dhd_cpkt_log_insert_ts(dhd_cpkt_log_ts_node_t *node, struct rb_root *root)
{
	struct rb_node **new = &root->rb_node, *parent = NULL;
	u64 ts_diff = node->ts_diff;

	while (*new) {
		parent = *new;
		if (ts_diff < rb_entry(parent, dhd_cpkt_log_ts_node_t, rb)->ts_diff)
			new = &parent->rb_left;
		else
			new = &parent->rb_right;
	}

	rb_link_node(&node->rb, parent, new);
	rb_insert_color(&node->rb, root);
}

static void
dhd_cpkt_log_deinit_tt(dhd_pub_t *dhdp)
{
	struct rb_node *n;
	dhd_pktlog_t *pktlog = dhdp->pktlog;

	dhd_cpkt_log_ts_node_t *node;

	while ((n = rb_first(&pktlog->cpkt_log_tt_rbt))) {
		node = rb_entry(n, dhd_cpkt_log_ts_node_t, rb);
		rb_erase(&node->rb, &pktlog->cpkt_log_tt_rbt);
		MFREE(dhdp->osh, node, sizeof(*node));
	}
}

static int
dhd_cpkt_log_init_tt(dhd_pub_t *dhdp)
{
	int i;
	int ret = BCME_OK;

	dhd_pktlog_t *pktlog = dhdp->pktlog;

	dhd_cpkt_log_ts_node_t *node;

	for (i = 0; i < ARRAYSIZE(dhd_cpkt_log_tt_idx); i++) {
		node = (dhd_cpkt_log_ts_node_t *)MALLOCZ(dhdp->osh, sizeof(*node));
		if (!node) {
			ret = BCME_NOMEM;
			goto exit;
		}
		node->ts_diff = dhd_cpkt_log_tt_idx[i];
		node->idx = i;

		dhd_cpkt_log_insert_ts(node, &pktlog->cpkt_log_tt_rbt);
	}

	return BCME_OK;
exit:
	dhd_cpkt_log_deinit_tt(dhdp);

	return ret;
}
#endif	/* DHD_COMPACT_PKT_LOG */
#endif /* DHD_PKT_LOGGING */
