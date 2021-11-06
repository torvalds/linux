/*
 * DHD debugability packet logging header file
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

#ifndef __DHD_PKTLOG_H_
#define __DHD_PKTLOG_H_

#include <dhd_debug.h>
#include <dhd.h>
#include <asm/atomic.h>
#ifdef DHD_COMPACT_PKT_LOG
#include <linux/rbtree.h>
#endif	/* DHD_COMPACT_PKT_LOG */

#ifdef DHD_PKT_LOGGING
#define DHD_PKT_LOG(args)	DHD_INFO(args)
#define DEFAULT_MULTIPLE_PKTLOG_BUF	1
#ifndef CUSTOM_MULTIPLE_PKTLOG_BUF
#define CUSTOM_MULTIPLE_PKTLOG_BUF	DEFAULT_MULTIPLE_PKTLOG_BUF
#endif /* CUSTOM_MULTIPLE_PKTLOG_BUF */
#define MIN_PKTLOG_LEN			(32 * 10 * 2 * CUSTOM_MULTIPLE_PKTLOG_BUF)
#define MAX_PKTLOG_LEN			(32 * 10 * 2 * 10)
#define MAX_DHD_PKTLOG_FILTER_LEN	14
#define MAX_MASK_PATTERN_FILTER_LEN	64
#define PKTLOG_TXPKT_CASE		0x0001
#define PKTLOG_TXSTATUS_CASE		0x0002
#define PKTLOG_RXPKT_CASE		0x0004
/* MAX_FILTER_PATTERN_LEN is buf len to print bitmask/pattern with string */
#define MAX_FILTER_PATTERN_LEN \
	((MAX_MASK_PATTERN_FILTER_LEN * HD_BYTE_SIZE) + HD_PREFIX_SIZE + 1) * 2
#define PKTLOG_DUMP_BUF_SIZE		(64 * 1024)

typedef struct dhd_dbg_pktlog_info {
	frame_type payload_type;
	size_t pkt_len;
	uint32 driver_ts_sec;
	uint32 driver_ts_usec;
	uint32 firmware_ts;
	uint32 pkt_hash;
	bool direction;
	void *pkt;
} dhd_dbg_pktlog_info_t;

typedef struct dhd_pktlog_ring_info
{
	dll_t p_info;			/* list pointer */
	union {
		wifi_tx_packet_fate tx_fate;
		wifi_rx_packet_fate rx_fate;
		uint32 fate;
	};
	dhd_dbg_pktlog_info_t info;
} dhd_pktlog_ring_info_t;

typedef struct dhd_pktlog_ring
{
	dll_t ring_info_head;		/* ring_info list */
	dll_t ring_info_free;		/* ring_info free list */
	osl_atomic_t start;
	uint32 pktlog_minmize;
	uint32 pktlog_len;		/* size of pkts */
	uint32 pktcount;
	spinlock_t *pktlog_ring_lock;
	dhd_pub_t *dhdp;
	dhd_pktlog_ring_info_t *ring_info_mem; /* ring_info mem pointer */
} dhd_pktlog_ring_t;

typedef struct dhd_pktlog_filter_info
{
	uint32 id;
	uint32 offset;
	uint32 size_bytes; /* Size of pattern. */
	uint32 enable;
	uint8 mask[MAX_MASK_PATTERN_FILTER_LEN];
	uint8 pattern[MAX_MASK_PATTERN_FILTER_LEN];
} dhd_pktlog_filter_info_t;

typedef struct dhd_pktlog_filter
{
	dhd_pktlog_filter_info_t *info;
	uint32 list_cnt;
	uint32 enable;
} dhd_pktlog_filter_t;

typedef struct dhd_pktlog
{
	struct dhd_pktlog_ring *pktlog_ring;
	struct dhd_pktlog_filter *pktlog_filter;
	osl_atomic_t pktlog_status;
	dhd_pub_t *dhdp;
#ifdef DHD_COMPACT_PKT_LOG
	struct rb_root cpkt_log_tt_rbt;
#endif  /* DHD_COMPACT_PKT_LOG */
} dhd_pktlog_t;

typedef struct dhd_pktlog_pcap_hdr
{
	uint32 magic_number;
	uint16 version_major;
	uint16 version_minor;
	uint16 thiszone;
	uint32 sigfigs;
	uint32 snaplen;
	uint32 network;
} dhd_pktlog_pcap_hdr_t;

#define PKTLOG_PCAP_MAGIC_NUM 0xa1b2c3d4
#define PKTLOG_PCAP_MAJOR_VER 0x02
#define PKTLOG_PCAP_MINOR_VER 0x04
#define PKTLOG_PCAP_SNAP_LEN 0x40000
#define PKTLOG_PCAP_NETWORK_TYPE 147

extern int dhd_os_attach_pktlog(dhd_pub_t *dhdp);
extern int dhd_os_detach_pktlog(dhd_pub_t *dhdp);
extern dhd_pktlog_ring_t* dhd_pktlog_ring_init(dhd_pub_t *dhdp, int size);
extern int dhd_pktlog_ring_deinit(dhd_pub_t *dhdp, dhd_pktlog_ring_t *ring);
extern int dhd_pktlog_ring_set_nextpos(dhd_pktlog_ring_t *ringbuf);
extern int dhd_pktlog_ring_get_nextbuf(dhd_pktlog_ring_t *ringbuf, void **data);
extern int dhd_pktlog_ring_set_prevpos(dhd_pktlog_ring_t *ringbuf);
extern int dhd_pktlog_ring_get_prevbuf(dhd_pktlog_ring_t *ringbuf, void **data);
extern int dhd_pktlog_ring_get_writebuf(dhd_pktlog_ring_t *ringbuf, void **data);
extern int dhd_pktlog_ring_add_pkts(dhd_pub_t *dhdp, void *pkt, void *pktdata, uint32 pktid,
		uint32 direction);
extern int dhd_pktlog_ring_tx_status(dhd_pub_t *dhdp, void *pkt, void *pktdata, uint32 pktid,
		uint16 status);
extern dhd_pktlog_ring_t* dhd_pktlog_ring_change_size(dhd_pktlog_ring_t *ringbuf, int size);
extern void dhd_pktlog_filter_pull_forward(dhd_pktlog_filter_t *filter,
		uint32 del_filter_id, uint32 list_cnt);

#define PKT_RX 0
#define PKT_TX 1
#define PKT_WAKERX 2
#define DHD_INVALID_PKTID (0U)
#define PKTLOG_TRANS_TX 0x01
#define PKTLOG_TRANS_RX 0x02
#define PKTLOG_TRANS_TXS 0x04

#define PKTLOG_SET_IN_TX(dhdp) \
{ \
	do { \
		OSL_ATOMIC_OR((dhdp)->osh, &(dhdp)->pktlog->pktlog_status, PKTLOG_TRANS_TX); \
	} while (0); \
}

#define PKTLOG_SET_IN_RX(dhdp) \
{ \
	do { \
		OSL_ATOMIC_OR((dhdp)->osh, &(dhdp)->pktlog->pktlog_status, PKTLOG_TRANS_RX); \
	} while (0); \
}

#define PKTLOG_SET_IN_TXS(dhdp) \
{ \
	do { \
		OSL_ATOMIC_OR((dhdp)->osh, &(dhdp)->pktlog->pktlog_status, PKTLOG_TRANS_TXS); \
	} while (0); \
}

#define PKTLOG_CLEAR_IN_TX(dhdp) \
{ \
	do { \
		OSL_ATOMIC_AND((dhdp)->osh, &(dhdp)->pktlog->pktlog_status, ~PKTLOG_TRANS_TX); \
	} while (0); \
}

#define PKTLOG_CLEAR_IN_RX(dhdp) \
{ \
	do { \
		OSL_ATOMIC_AND((dhdp)->osh, &(dhdp)->pktlog->pktlog_status, ~PKTLOG_TRANS_RX); \
	} while (0); \
}

#define PKTLOG_CLEAR_IN_TXS(dhdp) \
{ \
	do { \
		OSL_ATOMIC_AND((dhdp)->osh, &(dhdp)->pktlog->pktlog_status, ~PKTLOG_TRANS_TXS); \
	} while (0); \
}

#define DHD_PKTLOG_TX(dhdp, pkt, pktdata, pktid) \
{ \
	do { \
		if ((dhdp) && (dhdp)->pktlog && (pkt)) { \
			PKTLOG_SET_IN_TX(dhdp); \
			if ((dhdp)->pktlog->pktlog_ring && \
				OSL_ATOMIC_READ((dhdp)->osh, \
					(&(dhdp)->pktlog->pktlog_ring->start))) { \
				dhd_pktlog_ring_add_pkts(dhdp, pkt, pktdata, pktid, PKT_TX); \
			} \
			PKTLOG_CLEAR_IN_TX(dhdp); \
		} \
	} while (0); \
}

#define DHD_PKTLOG_TXS(dhdp, pkt, pktdata, pktid, status) \
{ \
	do { \
		if ((dhdp) && (dhdp)->pktlog && (pkt)) { \
			PKTLOG_SET_IN_TXS(dhdp); \
			if ((dhdp)->pktlog->pktlog_ring && \
				OSL_ATOMIC_READ((dhdp)->osh, \
					(&(dhdp)->pktlog->pktlog_ring->start))) { \
				dhd_pktlog_ring_tx_status(dhdp, pkt, pktdata, pktid, status); \
			} \
			PKTLOG_CLEAR_IN_TXS(dhdp); \
		} \
	} while (0); \
}

#define DHD_PKTLOG_RX(dhdp, pkt, pktdata) \
{ \
	do { \
		if ((dhdp) && (dhdp)->pktlog && (pkt)) { \
			PKTLOG_SET_IN_RX(dhdp); \
			if (ntoh16((pkt)->protocol) != ETHER_TYPE_BRCM) { \
				if ((dhdp)->pktlog->pktlog_ring && \
					OSL_ATOMIC_READ((dhdp)->osh, \
						(&(dhdp)->pktlog->pktlog_ring->start))) { \
					dhd_pktlog_ring_add_pkts(dhdp, pkt, pktdata, \
						DHD_INVALID_PKTID, PKT_RX); \
				} \
			} \
			PKTLOG_CLEAR_IN_RX(dhdp); \
		} \
	} while (0); \
}

#define DHD_PKTLOG_WAKERX(dhdp, pkt, pktdata) \
{ \
	do { \
		if ((dhdp) && (dhdp)->pktlog && (pkt)) { \
			PKTLOG_SET_IN_RX(dhdp); \
			if (ntoh16((pkt)->protocol) != ETHER_TYPE_BRCM) { \
				if ((dhdp)->pktlog->pktlog_ring && \
					OSL_ATOMIC_READ((dhdp)->osh, \
						(&(dhdp)->pktlog->pktlog_ring->start))) { \
					dhd_pktlog_ring_add_pkts(dhdp, pkt, pktdata, \
						DHD_INVALID_PKTID, PKT_WAKERX); \
				} \
			} \
			PKTLOG_CLEAR_IN_RX(dhdp); \
		} \
	} while (0); \
}

extern dhd_pktlog_filter_t* dhd_pktlog_filter_init(int size);
extern int dhd_pktlog_filter_deinit(dhd_pktlog_filter_t *filter);
extern int dhd_pktlog_filter_add(dhd_pktlog_filter_t *filter, char *arg);
extern int dhd_pktlog_filter_del(dhd_pktlog_filter_t *filter, char *arg);
extern int dhd_pktlog_filter_enable(dhd_pktlog_filter_t *filter, uint32 pktlog_case, uint32 enable);
extern int dhd_pktlog_filter_pattern_enable(dhd_pktlog_filter_t *filter, char *arg, uint32 enable);
extern int dhd_pktlog_filter_info(dhd_pktlog_filter_t *filter);
extern bool dhd_pktlog_filter_matched(dhd_pktlog_filter_t *filter, char *data, uint32 pktlog_case);
extern bool dhd_pktlog_filter_existed(dhd_pktlog_filter_t *filter, char *arg, uint32 *id);

#define DHD_PKTLOG_FILTER_ADD(pattern, filter_pattern, dhdp)	\
{	\
	do {	\
		if ((strlen(pattern) + 1) < sizeof(filter_pattern)) {	\
			strncpy(filter_pattern, pattern, sizeof(filter_pattern));	\
			dhd_pktlog_filter_add(dhdp->pktlog->pktlog_filter, filter_pattern);	\
		}	\
	} while (0);	\
}

#define DHD_PKTLOG_DUMP_PATH	DHD_COMMON_DUMP_PATH
extern int dhd_pktlog_debug_dump(dhd_pub_t *dhdp);
extern void dhd_pktlog_dump(void *handle, void *event_info, u8 event);
extern void dhd_schedule_pktlog_dump(dhd_pub_t *dhdp);
extern int dhd_pktlog_dump_write_memory(dhd_pub_t *dhdp, const void *user_buf, uint32 size);
extern int dhd_pktlog_dump_write_file(dhd_pub_t *dhdp);

#define DHD_PKTLOG_FATE_INFO_STR_LEN 256
#define DHD_PKTLOG_FATE_INFO_FORMAT	"BRCM_Packet_Fate"
#define DHD_PKTLOG_DUMP_TYPE "pktlog_dump"
#define DHD_PKTLOG_DEBUG_DUMP_TYPE "pktlog_debug_dump"

extern void dhd_pktlog_get_filename(dhd_pub_t *dhdp, char *dump_path, int len);
extern uint32 dhd_pktlog_get_item_length(dhd_pktlog_ring_info_t *report_ptr);
extern uint32 dhd_pktlog_get_dump_length(dhd_pub_t *dhdp);
extern uint32 __dhd_dbg_pkt_hash(uintptr_t pkt, uint32 pktid);

#ifdef DHD_COMPACT_PKT_LOG
#define CPKT_LOG_BIT_SIZE		22
#define CPKT_LOG_MAX_NUM		80
extern int dhd_cpkt_log_proc(dhd_pub_t *dhdp, char *buf, int buf_len,
        int bit_offset, int req_pkt_num);
#endif  /* DHD_COMPACT_PKT_LOG */
#endif /* DHD_PKT_LOGGING */
#endif /* __DHD_PKTLOG_H_ */
