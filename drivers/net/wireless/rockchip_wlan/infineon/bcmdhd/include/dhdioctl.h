/*
 * Definitions for ioctls to access DHD iovars.
 * Based on wlioctl.h (for Broadcom 802.11abg driver).
 * (Moves towards generic ioctls for BCM drivers/iovars.)
 *
 * Definitions subject to change without notice.
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhdioctl.h 697634 2017-05-04 11:02:38Z $
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

typedef enum {
	DMA_XFER_SUCCESS = 0,
	DMA_XFER_IN_PROGRESS,
	DMA_XFER_FAILED
} dma_xfer_status_t;

typedef enum d11_lpbk_type {
	M2M_DMA_LPBK = 0,
	D11_LPBK = 1,
	BMC_LPBK = 2,
	M2M_NON_DMA_LPBK = 3,
	D11_HOST_MEM_LPBK = 4,
	BMC_HOST_MEM_LPBK = 5,
	MAX_LPBK = 6
} dma_xfer_type_t;

typedef struct dmaxfer_info {
	uint16 version;
	uint16 length;
	dma_xfer_status_t status;
	dma_xfer_type_t type;
	uint src_delay;
	uint dest_delay;
	uint should_wait;
	uint core_num;
	int error_code;
	uint32 num_bytes;
	uint64 time_taken;
	uint64 tput;
} dma_xfer_info_t;

#define DHD_DMAXFER_VERSION 0x1

typedef struct tput_test {
	uint16 version;
	uint16 length;
	uint8 direction;
	uint8 tput_test_running;
	uint8 mac_sta[6];
	uint8 mac_ap[6];
	uint8 PAD[2];
	uint32 payload_size;
	uint32 num_pkts;
	uint32 timeout_ms;
	uint32 flags;

	uint32 pkts_good;
	uint32 pkts_bad;
	uint32 pkts_cmpl;
	uint64 time_ms;
	uint64 tput_bps;
} tput_test_t;

typedef enum {
	TPUT_DIR_TX = 0,
	TPUT_DIR_RX
} tput_dir_t;

#define TPUT_TEST_T_VER 1
#define TPUT_TEST_T_LEN 68
#define TPUT_TEST_MIN_PAYLOAD_SIZE 16
#define TPUT_TEST_USE_ETHERNET_HDR 0x1
#define TPUT_TEST_USE_802_11_HDR 0x2

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
#define DHD_WL_VAL		0x10000
#define DHD_NOCHECKDIED_VAL		0x20000 /* UTF WAR */
#define DHD_WL_VAL2		0x40000
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
#define DHD_DNGL_IOVAR_SET_VAL	0x10000000 /**< logs the setting of dongle iovars */
#define DHD_LPBKDTDUMP_VAL	0x20000000
#define DHD_PRSRV_MEM_VAL	0x40000000
#define DHD_IOVAR_MEM_VAL	0x80000000

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

enum dhd_maclist_xtlv_type {
	DHD_MACLIST_XTLV_R = 0x1,
	DHD_MACLIST_XTLV_X = 0x2,
	DHD_SVMPLIST_XTLV = 0x3
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

/* BT logging and memory dump */

#define BT_LOG_BUF_MAX_SIZE		(DHD_IOCTL_MAXLEN - (2 * sizeof(int)))
#define BT_LOG_BUF_NOT_AVAILABLE	0
#define BT_LOG_NEXT_BUF_NOT_AVAIL	1
#define BT_LOG_NEXT_BUF_AVAIL		2
#define BT_LOG_NOT_READY		3

typedef struct bt_log_buf_info {
	int availability;
	int size;
	char buf[BT_LOG_BUF_MAX_SIZE];
} bt_log_buf_info_t;

/* request BT memory in chunks */
typedef struct bt_mem_req {
	int offset;	/* offset from BT memory start */
	int buf_size;	/* buffer size per chunk */
} bt_mem_req_t;

/* max dest supported */
#define DEBUG_BUF_DEST_MAX	4

/* debug buf dest stat */
typedef struct debug_buf_dest_stat {
	uint32 stat[DEBUG_BUF_DEST_MAX];
} debug_buf_dest_stat_t;

#endif /* _dhdioctl_h_ */
