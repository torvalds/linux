/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __SELFTEST_RPC_H__
#define __SELFTEST_RPC_H__

#include "../../include/linux/lnet/lnetst.h"

/*
 * LST wired structures
 *
 * XXX: *REPLY == *REQST + 1
 */
typedef enum {
	SRPC_MSG_MKSN_REQST     = 0,
	SRPC_MSG_MKSN_REPLY     = 1,
	SRPC_MSG_RMSN_REQST     = 2,
	SRPC_MSG_RMSN_REPLY     = 3,
	SRPC_MSG_BATCH_REQST    = 4,
	SRPC_MSG_BATCH_REPLY    = 5,
	SRPC_MSG_STAT_REQST     = 6,
	SRPC_MSG_STAT_REPLY     = 7,
	SRPC_MSG_TEST_REQST     = 8,
	SRPC_MSG_TEST_REPLY     = 9,
	SRPC_MSG_DEBUG_REQST    = 10,
	SRPC_MSG_DEBUG_REPLY    = 11,
	SRPC_MSG_BRW_REQST      = 12,
	SRPC_MSG_BRW_REPLY      = 13,
	SRPC_MSG_PING_REQST     = 14,
	SRPC_MSG_PING_REPLY     = 15,
	SRPC_MSG_JOIN_REQST     = 16,
	SRPC_MSG_JOIN_REPLY     = 17,
} srpc_msg_type_t;

/* CAVEAT EMPTOR:
 * All srpc_*_reqst_t's 1st field must be matchbits of reply buffer,
 * and 2nd field matchbits of bulk buffer if any.
 *
 * All srpc_*_reply_t's 1st field must be a __u32 status, and 2nd field
 * session id if needed.
 */
typedef struct {
	__u64			rpyid;		/* reply buffer matchbits */
	__u64			bulkid;		/* bulk buffer matchbits */
} WIRE_ATTR srpc_generic_reqst_t;

typedef struct {
	__u32                   status;
	lst_sid_t               sid;
} WIRE_ATTR srpc_generic_reply_t;

/* FRAMEWORK RPCs */
typedef struct {
	__u64                   mksn_rpyid;     /* reply buffer matchbits */
	lst_sid_t               mksn_sid;	/* session id */
	__u32			mksn_force;     /* use brute force */
	char			mksn_name[LST_NAME_SIZE];
} WIRE_ATTR srpc_mksn_reqst_t; /* make session request */

typedef struct {
	__u32                   mksn_status;    /* session status */
	lst_sid_t               mksn_sid;       /* session id */
	__u32                   mksn_timeout;   /* session timeout */
	char                    mksn_name[LST_NAME_SIZE];
} WIRE_ATTR srpc_mksn_reply_t; /* make session reply */

typedef struct {
	__u64                   rmsn_rpyid;     /* reply buffer matchbits */
	lst_sid_t               rmsn_sid;       /* session id */
} WIRE_ATTR srpc_rmsn_reqst_t; /* remove session request */

typedef struct {
	__u32                   rmsn_status;
	lst_sid_t               rmsn_sid;       /* session id */
} WIRE_ATTR srpc_rmsn_reply_t; /* remove session reply */

typedef struct {
	__u64                   join_rpyid;     /* reply buffer matchbits */
	lst_sid_t               join_sid;       /* session id to join */
	char                    join_group[LST_NAME_SIZE]; /* group name */
} WIRE_ATTR srpc_join_reqst_t;

typedef struct {
	__u32                   join_status;    /* returned status */
	lst_sid_t               join_sid;       /* session id */
	__u32			join_timeout;   /* # seconds' inactivity to
						 * expire */
	char                    join_session[LST_NAME_SIZE]; /* session name */
} WIRE_ATTR srpc_join_reply_t;

typedef struct {
	__u64                   dbg_rpyid;      /* reply buffer matchbits */
	lst_sid_t               dbg_sid;        /* session id */
	__u32                   dbg_flags;      /* bitmap of debug */
} WIRE_ATTR srpc_debug_reqst_t;

typedef struct {
	__u32                   dbg_status;     /* returned code */
	lst_sid_t               dbg_sid;        /* session id */
	__u32                   dbg_timeout;    /* session timeout */
	__u32                   dbg_nbatch;     /* # of batches in the node */
	char                    dbg_name[LST_NAME_SIZE]; /* session name */
} WIRE_ATTR srpc_debug_reply_t;

#define SRPC_BATCH_OPC_RUN      1
#define SRPC_BATCH_OPC_STOP     2
#define SRPC_BATCH_OPC_QUERY    3

typedef struct {
	__u64              bar_rpyid;      /* reply buffer matchbits */
	lst_sid_t          bar_sid;        /* session id */
	lst_bid_t          bar_bid;        /* batch id */
	__u32              bar_opc;        /* create/start/stop batch */
	__u32              bar_testidx;    /* index of test */
	__u32              bar_arg;        /* parameters */
} WIRE_ATTR srpc_batch_reqst_t;

typedef struct {
	__u32              bar_status;     /* status of request */
	lst_sid_t          bar_sid;        /* session id */
	__u32              bar_active;     /* # of active tests in batch/test */
	__u32              bar_time;       /* remained time */
} WIRE_ATTR srpc_batch_reply_t;

typedef struct {
	__u64              str_rpyid;      /* reply buffer matchbits */
	lst_sid_t          str_sid;        /* session id */
	__u32              str_type;       /* type of stat */
} WIRE_ATTR srpc_stat_reqst_t;

typedef struct {
	__u32              str_status;
	lst_sid_t          str_sid;
	sfw_counters_t     str_fw;
	srpc_counters_t    str_rpc;
	lnet_counters_t    str_lnet;
} WIRE_ATTR srpc_stat_reply_t;

typedef struct {
	__u32              blk_opc;        /* bulk operation code */
	__u32              blk_npg;        /* # of pages */
	__u32              blk_flags;      /* reserved flags */
} WIRE_ATTR test_bulk_req_t;

typedef struct {
	__u16              blk_opc;        /* bulk operation code */
	__u16              blk_flags;      /* data check flags */
	__u32              blk_len;        /* data length */
	__u32              blk_offset;     /* reserved: offset */
} WIRE_ATTR test_bulk_req_v1_t;

typedef struct {
	__u32              png_size;       /* size of ping message */
	__u32              png_flags;      /* reserved flags */
} WIRE_ATTR test_ping_req_t;

typedef struct {
	__u64			tsr_rpyid;      /* reply buffer matchbits */
	__u64			tsr_bulkid;     /* bulk buffer matchbits */
	lst_sid_t		tsr_sid;	/* session id */
	lst_bid_t		tsr_bid;	/* batch id */
	__u32			tsr_service;    /* test type: bulk|ping|... */
	__u32			tsr_loop;       /* test client loop count or
						 * # server buffers needed */
	__u32			tsr_concur;     /* concurrency of test */
	__u8			tsr_is_client;  /* is test client or not */
	__u8			tsr_stop_onerr; /* stop on error */
	__u32			tsr_ndest;      /* # of dest nodes */

	union {
		test_ping_req_t		ping;
		test_bulk_req_t		bulk_v0;
		test_bulk_req_v1_t	bulk_v1;
	}		tsr_u;
} WIRE_ATTR srpc_test_reqst_t;

typedef struct {
	__u32			tsr_status;     /* returned code */
	lst_sid_t		tsr_sid;
} WIRE_ATTR srpc_test_reply_t;

/* TEST RPCs */
typedef struct {
	__u64		   pnr_rpyid;
	__u32		   pnr_magic;
	__u32		   pnr_seq;
	__u64		   pnr_time_sec;
	__u64		   pnr_time_usec;
} WIRE_ATTR srpc_ping_reqst_t;

typedef struct {
	__u32		   pnr_status;
	__u32		   pnr_magic;
	__u32		   pnr_seq;
} WIRE_ATTR srpc_ping_reply_t;

typedef struct {
	__u64		   brw_rpyid;      /* reply buffer matchbits */
	__u64		   brw_bulkid;     /* bulk buffer matchbits */
	__u32		   brw_rw;         /* read or write */
	__u32		   brw_len;        /* bulk data len */
	__u32		   brw_flags;      /* bulk data patterns */
} WIRE_ATTR srpc_brw_reqst_t; /* bulk r/w request */

typedef struct {
	__u32		   brw_status;
} WIRE_ATTR srpc_brw_reply_t; /* bulk r/w reply */

#define SRPC_MSG_MAGIC   0xeeb0f00d
#define SRPC_MSG_VERSION 1

typedef struct srpc_msg {
	__u32	msg_magic;     /* magic number */
	__u32	msg_version;   /* message version number */
	__u32	msg_type;      /* type of message body: srpc_msg_type_t */
	__u32	msg_reserved0;
	__u32	msg_reserved1;
	__u32	msg_ses_feats; /* test session features */
	union {
		srpc_generic_reqst_t reqst;
		srpc_generic_reply_t reply;

		srpc_mksn_reqst_t    mksn_reqst;
		srpc_mksn_reply_t    mksn_reply;
		srpc_rmsn_reqst_t    rmsn_reqst;
		srpc_rmsn_reply_t    rmsn_reply;
		srpc_debug_reqst_t   dbg_reqst;
		srpc_debug_reply_t   dbg_reply;
		srpc_batch_reqst_t   bat_reqst;
		srpc_batch_reply_t   bat_reply;
		srpc_stat_reqst_t    stat_reqst;
		srpc_stat_reply_t    stat_reply;
		srpc_test_reqst_t    tes_reqst;
		srpc_test_reply_t    tes_reply;
		srpc_join_reqst_t    join_reqst;
		srpc_join_reply_t    join_reply;

		srpc_ping_reqst_t    ping_reqst;
		srpc_ping_reply_t    ping_reply;
		srpc_brw_reqst_t     brw_reqst;
		srpc_brw_reply_t     brw_reply;
	}     msg_body;
} WIRE_ATTR srpc_msg_t;

static inline void
srpc_unpack_msg_hdr(srpc_msg_t *msg)
{
	if (msg->msg_magic == SRPC_MSG_MAGIC)
		return; /* no flipping needed */

	/* We do not swap the magic number here as it is needed to
	   determine whether the body needs to be swapped. */
	/* __swab32s(&msg->msg_magic); */
	__swab32s(&msg->msg_type);
	__swab32s(&msg->msg_version);
	__swab32s(&msg->msg_ses_feats);
	__swab32s(&msg->msg_reserved0);
	__swab32s(&msg->msg_reserved1);
}

#endif /* __SELFTEST_RPC_H__ */
