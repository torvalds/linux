/*
 * Definitions for ioctls to access DHD iovars.
 * Based on wlioctl.h (for Broadcom 802.11abg driver).
 * (Moves towards generic ioctls for BCM drivers/iovars.)
 *
 * Definitions subject to change without notice.
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhdioctl.h 675190 2016-12-14 15:27:52Z $
 */

#ifndef _dhdioctl_h_
#define	_dhdioctl_h_

#include <typedefs.h>


/* Linux network driver ioctl encoding */
typedef struct dhd_ioctl {
	uint32 cmd;	/* common ioctl definition */
	void *buf;	/* pointer to user buffer */
	uint32 len;	/* length of user buffer */
	uint32 set;	/* get or set request boolean (optional) */
	uint32 used;	/* bytes read or written (optional) */
	uint32 needed;	/* bytes needed (optional) */
	uint32 driver;	/* to identify target driver */
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

/*
 * Increase the DHD_IOCTL_MAXLEN to 16K for supporting download of NVRAM files of size
 * > 8K. In the existing implementation when NVRAM is to be downloaded via the "vars"
 * DHD IOVAR, the NVRAM is copied to the DHD Driver memory. Later on when "dwnldstate" is
 * invoked with FALSE option, the NVRAM gets copied from the DHD driver to the Dongle
 * memory. The simple way to support this feature without modifying the DHD application,
 * driver logic is to increase the DHD_IOCTL_MAXLEN size. This macro defines the "size"
 * of the buffer in which data is exchanged between the DHD App and DHD driver.
 */
#define	DHD_IOCTL_MAXLEN	(16384)	/* max length ioctl buffer required */
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
#define DHD_NOCHECKDIED_VAL		0x20000 /* UTF WAR */
#define DHD_PNO_VAL		0x80000
#define DHD_RTT_VAL		0x100000
#define DHD_MSGTRACE_VAL	0x200000
#define DHD_FWLOG_VAL		0x400000
#define DHD_DBGIF_VAL		0x800000
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
#define DHD_RPM_VAL		0x1000000
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
#define DHD_PKT_MON_VAL		0x2000000
#define DHD_PKT_MON_DUMP_VAL	0x4000000
#define DHD_ERROR_MEM_VAL	0x8000000
#define DHD_ANDROID_VAL	0x10000
#define DHD_IW_VAL	0x20000
#define DHD_CFG_VAL	0x40000
#define DHD_CONFIG_VAL	0x80000
#define DUMP_EAPOL_VAL	0x0001
#define DUMP_ARP_VAL	0x0002
#define DUMP_DHCP_VAL	0x0004
#define DUMP_ICMP_VAL	0x0008
#define DUMP_DNS_VAL	0x0010
#define DUMP_TRX_VAL	0x0080

#ifdef SDTEST
/* For pktgen iovar */
typedef struct dhd_pktgen {
	uint32 version;		/* To allow structure change tracking */
	uint32 freq;		/* Max ticks between tx/rx attempts */
	uint32 count;		/* Test packets to send/rcv each attempt */
	uint32 print;		/* Print counts every <print> attempts */
	uint32 total;		/* Total packets (or bursts) */
	uint32 minlen;		/* Minimum length of packets to send */
	uint32 maxlen;		/* Maximum length of packets to send */
	uint32 numsent;		/* Count of test packets sent */
	uint32 numrcvd;		/* Count of test packets received */
	uint32 numfail;		/* Count of test send failures */
	uint32 mode;		/* Test mode (type of test packets) */
	uint32 stop;		/* Stop after this many tx failures */
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



#endif /* _dhdioctl_h_ */
