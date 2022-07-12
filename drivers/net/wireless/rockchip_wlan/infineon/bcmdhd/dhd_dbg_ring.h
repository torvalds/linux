/*
 * DHD debug ring header file
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
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
 * $Id$
 */

#ifndef __DHD_DBG_RING_H__
#define __DHD_DBG_RING_H__

#include <bcmutils.h>

#define PACKED_STRUCT __attribute__ ((packed))

#define DBGRING_NAME_MAX 32

enum dbg_ring_state {
	RING_STOP = 0,  /* ring is not initialized */
	RING_ACTIVE,    /* ring is live and logging */
	RING_SUSPEND    /* ring is initialized but not logging */
};

/* each entry in dbg ring has below header, to handle
 * variable length records in ring
 */
typedef struct dhd_dbg_ring_entry {
	uint16 len; /* payload length excluding the header */
	uint8 flags;
	uint8 type; /* Per ring specific */
	uint64 timestamp; /* present if has_timestamp bit is set. */
} PACKED_STRUCT dhd_dbg_ring_entry_t;

struct ring_statistics {
	/* number of bytes that was written to the buffer by driver */
	uint32 written_bytes;
	/* number of bytes that was read from the buffer by user land */
	uint32 read_bytes;
	/* number of records that was written to the buffer by driver */
	uint32 written_records;
};

typedef struct dhd_dbg_ring_status {
	uint8 name[DBGRING_NAME_MAX];
	uint32 flags;
	int ring_id; /* unique integer representing the ring */
	/* total memory size allocated for the buffer */
	uint32 ring_buffer_byte_size;
	uint32 verbose_level;
	/* number of bytes that was written to the buffer by driver */
	uint32 written_bytes;
	/* number of bytes that was read from the buffer by user land */
	uint32 read_bytes;
	/* number of records that was read from the buffer by user land */
	uint32 written_records;
} dhd_dbg_ring_status_t;

typedef struct dhd_dbg_ring {
	int     id;		/* ring id */
	uint8   name[DBGRING_NAME_MAX]; /* name string */
	uint32  ring_size;	/* numbers of item in ring */
	uint32  wp;		/* write pointer */
	uint32  rp;		/* read pointer */
	uint32  rp_tmp;		/* tmp read pointer */
	uint32  log_level;	/* log_level */
	uint32  threshold;	/* threshold bytes */
	void *  ring_buf;	/* pointer of actually ring buffer */
	void *  lock;		/* lock for ring access */
	struct ring_statistics stat;	/* statistics */
	enum dbg_ring_state state;	/* ring state enum */
	bool tail_padded;	/* writer does not have enough space */
	uint32 rem_len;		/* number of bytes from wp_pad to end */
	bool sched_pull;	/* schedule reader immediately */
	bool pull_inactive;	/* pull contents from ring even if it is inactive */
} dhd_dbg_ring_t;

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

#define DBG_RING_ENTRY_SIZE (sizeof(dhd_dbg_ring_entry_t))
#define ENTRY_LENGTH(hdr) ((hdr)->len + DBG_RING_ENTRY_SIZE)
#define PAYLOAD_MAX_LEN 65535
#define PAYLOAD_ECNTR_MAX_LEN 1648u
#define PAYLOAD_RTT_MAX_LEN 1648u
#define PENDING_LEN_MAX 0xFFFFFFFF
#define DBG_RING_STATUS_SIZE (sizeof(dhd_dbg_ring_status_t))

#define TXACTIVESZ(r, w, d)			(((r) <= (w)) ? ((w) - (r)) : ((d) - (r) + (w)))
#define DBG_RING_READ_AVAIL_SPACE(w, r, d)		(((w) >= (r)) ? ((w) - (r)) : ((d) - (r)))
#define DBG_RING_WRITE_SPACE_AVAIL_CONT(r, w, d)	(((w) >= (r)) ? ((d) - (w)) : ((r) - (w)))
#define DBG_RING_WRITE_SPACE_AVAIL(r, w, d)		(d - (TXACTIVESZ(r, w, d)))
#define DBG_RING_CHECK_WRITE_SPACE(r, w, d)		\
		MIN(DBG_RING_WRITE_SPACE_AVAIL(r, w, d), DBG_RING_WRITE_SPACE_AVAIL_CONT(r, w, d))

typedef void (*os_pullreq_t)(void *os_priv, const int ring_id);

int dhd_dbg_ring_init(dhd_pub_t *dhdp, dhd_dbg_ring_t *ring, uint16 id, uint8 *name,
		uint32 ring_sz, void *allocd_buf, bool pull_inactive);
void dhd_dbg_ring_deinit(dhd_pub_t *dhdp, dhd_dbg_ring_t *ring);
int dhd_dbg_ring_push(dhd_dbg_ring_t *ring, dhd_dbg_ring_entry_t *hdr, void *data);
int dhd_dbg_ring_pull(dhd_dbg_ring_t *ring, void *data, uint32 buf_len,
		bool strip_hdr);
int dhd_dbg_ring_pull_single(dhd_dbg_ring_t *ring, void *data, uint32 buf_len,
	bool strip_header);
uint32 dhd_dbg_ring_get_pending_len(dhd_dbg_ring_t *ring);
void dhd_dbg_ring_sched_pull(dhd_dbg_ring_t *ring, uint32 pending_len,
		os_pullreq_t pull_fn, void *os_pvt, const int id);
int dhd_dbg_ring_config(dhd_dbg_ring_t *ring, int log_level, uint32 threshold);
void dhd_dbg_ring_start(dhd_dbg_ring_t *ring);
#endif /* __DHD_DBG_RING_H__ */
