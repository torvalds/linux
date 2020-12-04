/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for ioctls to access DHD iovars.
 * Based on wlioctl.h (for Broadcom 802.11abg driver).
 * (Moves towards generic ioctls for BCM drivers/iovars.)
 *
 * Definitions subject to change without notice.
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhdioctl.h 603083 2015-11-30 23:40:43Z $
 */

#ifndef _dhdioctl_h_
#define	_dhdioctl_h_

#include <typedefs.h>


/* require default structure packing */
#define BWL_DEFAULT_PACKING
#include <packed_section_start.h>


/* Linux network driver ioctl encoding */
typedef struct dhd_ioctl {
	uint cmd;	/* common ioctl definition */
	void *buf;	/* pointer to user buffer */
	uint len;	/* length of user buffer */
	bool set;	/* get or set request (optional) */
	uint used;	/* bytes read or written (optional) */
	uint needed;	/* bytes needed (optional) */
	uint driver;	/* to identify target driver */
} dhd_ioctl_t;

/* Underlying BUS definition */
enum {
	BUS_TYPE_USB = 0, /* for USB dongles */
	BUS_TYPE_SDIO, /* for SDIO dongles */
	BUS_TYPE_PCIE /* for PCIE dongles */
};

/* per-driver magic numbers */
#define DHD_IOCTL_MAGIC		0x00444944

/* bump this number if you change the ioctl interface */
#define DHD_IOCTL_VERSION	1

#define	DHD_IOCTL_MAXLEN	8192		/* max length ioctl buffer required */
#define	DHD_IOCTL_SMLEN		256		/* "small" length ioctl buffer required */

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
#define DHD_ISCAN_VAL	0x2000
#define DHD_ARPOE_VAL	0x4000
#define DHD_REORDER_VAL	0x8000
#define DHD_WL_VAL		0x10000
#define DHD_NOCHECKDIED_VAL		0x20000 /* UTF WAR */
#define DHD_WL_VAL2		0x40000
#define DHD_PNO_VAL		0x80000
#define DHD_MSGTRACE_VAL	0x100000
#define DHD_FWLOG_VAL		0x400000
#define DHD_RTT_VAL		0x200000
#define DHD_DBGIF_VAL		0x800000
#define DHD_DBG_BCNRX_VAL	0x1000000

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
#define DHD_PKTGEN_ECHO		1 /* Send echo requests */
#define DHD_PKTGEN_SEND 	2 /* Send discard packets */
#define DHD_PKTGEN_RXBURST	3 /* Request dongle send N packets */
#define DHD_PKTGEN_RECV		4 /* Continuous rx from continuous tx dongle */
#endif /* SDTEST */

/* Enter idle immediately (no timeout) */
#define DHD_IDLE_IMMEDIATE	(-1)

/* Values for idleclock iovar: other values are the sd_divisor to use when idle */
#define DHD_IDLE_ACTIVE	0	/* Do not request any SD clock change when idle */
#define DHD_IDLE_STOP   (-1)	/* Request SD clock be stopped (and use SD1 mode) */


enum dhd_maclist_xtlv_type {
	DHD_MACLIST_XTLV_R = 0x1,
	DHD_MACLIST_XTLV_X = 0x2
};

typedef struct _dhd_maclist_t {
	uint16 version;		/* Version */
	uint16 bytes_len;	/* Total bytes length of lists, XTLV headers and paddings */
	uint8 plist[1];		/* Pointer to the first list */
} dhd_maclist_t;

typedef struct _dhd_pd11regs_param {
	uint16 start_idx;
	uint8 verbose;
	uint8 pad;
	uint8 plist[1];
} dhd_pd11regs_param;

typedef struct _dhd_pd11regs_buf {
	uint16 idx;
	uint8 pad[2];
	uint8 pbuf[1];
} dhd_pd11regs_buf;

/* require default structure packing */
#include <packed_section_end.h>

#endif /* _dhdioctl_h_ */
