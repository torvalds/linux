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
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/include/lnet/ptllnd_wire.h
 *
 * Author: PJ Kirner <pjkirner@clusterfs.com>
 */

/* Minimum buffer size that any peer will post to receive ptllnd messages */
#define PTLLND_MIN_BUFFER_SIZE  256

/************************************************************************
 * Tunable defaults that {u,k}lnds/ptllnd should have in common.
 */

#define PTLLND_PORTAL	   9	  /* The same portal PTLPRC used when talking to cray portals */
#define PTLLND_PID	      9	  /* The Portals PID */
#define PTLLND_PEERCREDITS      8	  /* concurrent sends to 1 peer */

/* Default buffer size for kernel ptllnds (guaranteed eager) */
#define PTLLND_MAX_KLND_MSG_SIZE 512

/* Default buffer size for catamount ptllnds (not guaranteed eager) - large
 * enough to avoid RDMA for anything sent while control is not in liblustre */
#define PTLLND_MAX_ULND_MSG_SIZE 512

/************************************************************************
 * Portals LND Wire message format.
 * These are sent in sender's byte order (i.e. receiver flips).
 */

#define PTL_RESERVED_MATCHBITS  0x100	/* below this value is reserved
					 * above is for bulk data transfer */
#define LNET_MSG_MATCHBITS       0      /* the value for the message channel */

typedef struct {
	lnet_hdr_t	kptlim_hdr;	     /* portals header */
	char	      kptlim_payload[0];      /* piggy-backed payload */
} WIRE_ATTR kptl_immediate_msg_t;

typedef struct {
	lnet_hdr_t	kptlrm_hdr;	     /* portals header */
	__u64	     kptlrm_matchbits;       /* matchbits */
} WIRE_ATTR kptl_rdma_msg_t;

typedef struct {
	__u64	     kptlhm_matchbits;       /* matchbits */
	__u32	     kptlhm_max_msg_size;    /* max message size */
} WIRE_ATTR kptl_hello_msg_t;

typedef struct {
	/* First 2 fields fixed FOR ALL TIME */
	__u32	   ptlm_magic;     /* I'm a Portals LND message */
	__u16	   ptlm_version;   /* this is my version number */
	__u8	    ptlm_type;      /* the message type */
	__u8	    ptlm_credits;   /* returned credits */
	__u32	   ptlm_nob;       /* # bytes in whole message */
	__u32	   ptlm_cksum;     /* checksum (0 == no checksum) */
	__u64	   ptlm_srcnid;    /* sender's NID */
	__u64	   ptlm_srcstamp;  /* sender's incarnation */
	__u64	   ptlm_dstnid;    /* destination's NID */
	__u64	   ptlm_dststamp;  /* destination's incarnation */
	__u32	   ptlm_srcpid;    /* sender's PID */
	__u32	   ptlm_dstpid;    /* destination's PID */

	 union {
		kptl_immediate_msg_t    immediate;
		kptl_rdma_msg_t	 rdma;
		kptl_hello_msg_t	hello;
	} WIRE_ATTR ptlm_u;

} kptl_msg_t;

/* kptl_msg_t::ptlm_credits is only a __u8 */
#define PTLLND_MSG_MAX_CREDITS ((typeof(((kptl_msg_t *)0)->ptlm_credits)) - 1)

#define PTLLND_MSG_MAGIC		LNET_PROTO_PTL_MAGIC
#define PTLLND_MSG_VERSION	      0x04

#define PTLLND_RDMA_OK		  0x00
#define PTLLND_RDMA_FAIL		0x01

#define PTLLND_MSG_TYPE_INVALID	 0x00
#define PTLLND_MSG_TYPE_PUT	     0x01
#define PTLLND_MSG_TYPE_GET	     0x02
#define PTLLND_MSG_TYPE_IMMEDIATE       0x03    /* No bulk data xfer*/
#define PTLLND_MSG_TYPE_NOOP	    0x04
#define PTLLND_MSG_TYPE_HELLO	   0x05
#define PTLLND_MSG_TYPE_NAK	     0x06
