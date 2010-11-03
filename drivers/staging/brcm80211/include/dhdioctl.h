/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _dhdioctl_h_
#define	_dhdioctl_h_

/* require default structure packing */
#define BWL_DEFAULT_PACKING
#include <packed_section_start.h>

/* Linux network driver ioctl encoding */
typedef struct dhd_ioctl {
	uint cmd;		/* common ioctl definition */
	void *buf;		/* pointer to user buffer */
	uint len;		/* length of user buffer */
	bool set;		/* get or set request (optional) */
	uint used;		/* bytes read or written (optional) */
	uint needed;		/* bytes needed (optional) */
	uint driver;		/* to identify target driver */
} dhd_ioctl_t;

/* per-driver magic numbers */
#define DHD_IOCTL_MAGIC		0x00444944

/* bump this number if you change the ioctl interface */
#define DHD_IOCTL_VERSION	1

#define	DHD_IOCTL_MAXLEN	8192	/* max length ioctl buffer required */
#define	DHD_IOCTL_SMLEN	256	/* "small" length ioctl buffer required */

/* common ioctl definitions */
#define DHD_GET_MAGIC				0
#define DHD_GET_VERSION				1
#define DHD_GET_VAR				2
#define DHD_SET_VAR				3

/* message levels */
#define DHD_ERROR_VAL	0x0001
#define DHD_TRACE_VAL	0x0002
#define DHD_INFO_VAL	0x0004
#define DHD_DATA_VAL	0x0008
#define DHD_CTL_VAL	0x0010
#define DHD_TIMER_VAL	0x0020
#define DHD_HDRS_VAL	0x0040
#define DHD_BYTES_VAL	0x0080
#define DHD_INTR_VAL	0x0100
#define DHD_LOG_VAL	0x0200
#define DHD_GLOM_VAL	0x0400
#define DHD_EVENT_VAL	0x0800
#define DHD_BTA_VAL	0x1000
#define DHD_ISCAN_VAL 0x2000

#ifdef SDTEST
/* For pktgen iovar */
typedef struct dhd_pktgen {
	uint version;		/* To allow structure change tracking */
	uint freq;		/* Max ticks between tx/rx attempts */
	uint count;		/* Test packets to send/rcv each attempt */
	uint print;		/* Print counts every <print> attempts */
	uint total;		/* Total packets (or bursts) */
	uint minlen;		/* Minimum length of packets to send */
	uint maxlen;		/* Maximum length of packets to send */
	uint numsent;		/* Count of test packets sent */
	uint numrcvd;		/* Count of test packets received */
	uint numfail;		/* Count of test send failures */
	uint mode;		/* Test mode (type of test packets) */
	uint stop;		/* Stop after this many tx failures */
} dhd_pktgen_t;

/* Version in case structure changes */
#define DHD_PKTGEN_VERSION 2

/* Type of test packets to use */
#define DHD_PKTGEN_ECHO		1	/* Send echo requests */
#define DHD_PKTGEN_SEND		2	/* Send discard packets */
#define DHD_PKTGEN_RXBURST	3	/* Request dongle send N packets */
#define DHD_PKTGEN_RECV		4	/* Continuous rx from continuous
					 tx dongle */
#endif				/* SDTEST */

/* Enter idle immediately (no timeout) */
#define DHD_IDLE_IMMEDIATE	(-1)

/* Values for idleclock iovar: other values are the sd_divisor to use
	 when idle */
#define DHD_IDLE_ACTIVE	0	/* Do not request any SD clock change
				 when idle */
#define DHD_IDLE_STOP   (-1)	/* Request SD clock be stopped
				 (and use SD1 mode) */

/* require default structure packing */
#include <packed_section_end.h>

#endif				/* _dhdioctl_h_ */
