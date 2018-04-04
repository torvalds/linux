/*
 * EVENT_LOG system definitions
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
 * $Id: event_log_tag.h 607675 2015-12-21 22:08:55Z $
 */

#ifndef _EVENT_LOG_TAG_H_
#define _EVENT_LOG_TAG_H_

#include <typedefs.h>

/* Define new event log tags here */
#define EVENT_LOG_TAG_NULL	0	/* Special null tag */
#define EVENT_LOG_TAG_TS	1	/* Special timestamp tag */
#define EVENT_LOG_TAG_BUS_OOB	2
#define EVENT_LOG_TAG_BUS_STATE	3
#define EVENT_LOG_TAG_BUS_PROTO	4
#define EVENT_LOG_TAG_BUS_CTL	5
#define EVENT_LOG_TAG_BUS_EVENT	6
#define EVENT_LOG_TAG_BUS_PKT	7
#define EVENT_LOG_TAG_BUS_FRAME	8
#define EVENT_LOG_TAG_BUS_DESC	9
#define EVENT_LOG_TAG_BUS_SETUP	10
#define EVENT_LOG_TAG_BUS_MISC	11
#define EVENT_LOG_TAG_SRSCAN		22
#define EVENT_LOG_TAG_PWRSTATS_INFO	23
#define EVENT_LOG_TAG_UCODE_WATCHDOG 26
#define EVENT_LOG_TAG_UCODE_FIFO 27
#define EVENT_LOG_TAG_SCAN_TRACE_LOW	28
#define EVENT_LOG_TAG_SCAN_TRACE_HIGH	29
#define EVENT_LOG_TAG_SCAN_ERROR	30
#define EVENT_LOG_TAG_SCAN_WARN	31
#define EVENT_LOG_TAG_MPF_ERR	32
#define EVENT_LOG_TAG_MPF_WARN	33
#define EVENT_LOG_TAG_MPF_INFO	34
#define EVENT_LOG_TAG_MPF_DEBUG	35
#define EVENT_LOG_TAG_EVENT_INFO	36
#define EVENT_LOG_TAG_EVENT_ERR	37
#define EVENT_LOG_TAG_PWRSTATS_ERROR	38
#define EVENT_LOG_TAG_EXCESS_PM_ERROR	39
#define EVENT_LOG_TAG_IOCTL_LOG			40
#define EVENT_LOG_TAG_PFN_ERR	41
#define EVENT_LOG_TAG_PFN_WARN	42
#define EVENT_LOG_TAG_PFN_INFO	43
#define EVENT_LOG_TAG_PFN_DEBUG	44
#define EVENT_LOG_TAG_BEACON_LOG	45
#define EVENT_LOG_TAG_WNM_BSSTRANS_INFO 46
#define EVENT_LOG_TAG_TRACE_CHANSW 47
#define EVENT_LOG_TAG_PCI_ERROR	48
#define EVENT_LOG_TAG_PCI_TRACE	49
#define EVENT_LOG_TAG_PCI_WARN	50
#define EVENT_LOG_TAG_PCI_INFO	51
#define EVENT_LOG_TAG_PCI_DBG	52
#define EVENT_LOG_TAG_PCI_DATA  53
#define EVENT_LOG_TAG_PCI_RING	54
#define EVENT_LOG_TAG_AWDL_TRACE_RANGING	55
#define EVENT_LOG_TAG_WL_ERROR		56
#define EVENT_LOG_TAG_PHY_ERROR		57
#define EVENT_LOG_TAG_OTP_ERROR		58
#define EVENT_LOG_TAG_NOTIF_ERROR	59
#define EVENT_LOG_TAG_MPOOL_ERROR	60
#define EVENT_LOG_TAG_OBJR_ERROR	61
#define EVENT_LOG_TAG_DMA_ERROR		62
#define EVENT_LOG_TAG_PMU_ERROR		63
#define EVENT_LOG_TAG_BSROM_ERROR	64
#define EVENT_LOG_TAG_SI_ERROR		65
#define EVENT_LOG_TAG_ROM_PRINTF	66
#define EVENT_LOG_TAG_RATE_CNT		67
#define EVENT_LOG_TAG_CTL_MGT_CNT	68
#define EVENT_LOG_TAG_AMPDU_DUMP	69
#define EVENT_LOG_TAG_MEM_ALLOC_SUCC	70
#define EVENT_LOG_TAG_MEM_ALLOC_FAIL	71
#define EVENT_LOG_TAG_MEM_FREE		72
#define EVENT_LOG_TAG_WL_ASSOC_LOG	73
#define EVENT_LOG_TAG_WL_PS_LOG		74
#define EVENT_LOG_TAG_WL_ROAM_LOG	75
#define EVENT_LOG_TAG_WL_MPC_LOG	76
#define EVENT_LOG_TAG_WL_WSEC_LOG	77
#define EVENT_LOG_TAG_WL_WSEC_DUMP	78
#define EVENT_LOG_TAG_WL_MCNX_LOG	79
#define EVENT_LOG_TAG_HEALTH_CHECK_ERROR 80
#define EVENT_LOG_TAG_HNDRTE_EVENT_ERROR 81
#define EVENT_LOG_TAG_ECOUNTERS_ERROR	82
#define EVENT_LOG_TAG_WL_COUNTERS	83
#define EVENT_LOG_TAG_ECOUNTERS_IPCSTATS	84
#define EVENT_LOG_TAG_WL_P2P_LOG            85
#define EVENT_LOG_TAG_SDIO_ERROR		86
#define EVENT_LOG_TAG_SDIO_TRACE		87
#define EVENT_LOG_TAG_SDIO_DBG          88
#define EVENT_LOG_TAG_SDIO_PRHDRS		89
#define EVENT_LOG_TAG_SDIO_PRPKT		90
#define EVENT_LOG_TAG_SDIO_INFORM		91
#define EVENT_LOG_TAG_MIMO_PS_ERROR	92
#define EVENT_LOG_TAG_MIMO_PS_TRACE	93
#define EVENT_LOG_TAG_MIMO_PS_INFO	94
#define EVENT_LOG_TAG_BTCX_STATS	95
#define EVENT_LOG_TAG_LEAKY_AP_STATS	96
#define EVENT_LOG_TAG_AWDL_TRACE_ELECTION	97
#define EVENT_LOG_TAG_MIMO_PS_STATS	98
#define EVENT_LOG_TAG_PWRSTATS_PHY	99
#define EVENT_LOG_TAG_PWRSTATS_SCAN	100
#define EVENT_LOG_TAG_PWRSTATS_AWDL	101
#define EVENT_LOG_TAG_PWRSTATS_WAKE_V2	102
#define EVENT_LOG_TAG_LQM		103
#define EVENT_LOG_TAG_TRACE_WL_INFO	104
#define EVENT_LOG_TAG_TRACE_BTCOEX_INFO	105
#define EVENT_LOG_TAG_ECOUNTERS_TIME_DATA	106
#define EVENT_LOG_TAG_NAN_ERROR		107
#define EVENT_LOG_TAG_NAN_INFO		108
#define EVENT_LOG_TAG_NAN_DBG		109
#define EVENT_LOG_TAG_STF_ARBITRATOR_ERROR	110
#define EVENT_LOG_TAG_STF_ARBITRATOR_TRACE	111
#define EVENT_LOG_TAG_STF_ARBITRATOR_WARN	112
#define EVENT_LOG_TAG_SCAN_SUMMARY		113
#define EVENT_LOG_TAG_PROXD_SAMPLE_COLLECT	114
#define EVENT_LOG_TAG_OCL_INFO			115
/* EVENT_LOG_TAG_MAX	= Set to the same value of last tag, not last tag + 1 */
#define EVENT_LOG_TAG_MAX			115
/* Note: New event should be added/reserved in trunk before adding it to branches */


#define	SD_PRHDRS(i, s, h, p, n, l)
#define	SD_PRPKT(m, b, n)
#define	SD_INFORM(args)

/* Flags for tag control */
#define EVENT_LOG_TAG_FLAG_NONE		0
#define EVENT_LOG_TAG_FLAG_LOG		0x80
#define EVENT_LOG_TAG_FLAG_PRINT	0x40
#define EVENT_LOG_TAG_FLAG_SET_MASK	0x3f

/* Each event log entry has a type.  The type is the LAST word of the
 * event log.  The printing code walks the event entries in reverse
 * order to find the first entry.
 */
typedef union event_log_hdr {
	struct {
		uint8 tag;		/* Event_log entry tag */
		uint8 count;		/* Count of 4-byte entries */
		uint16 fmt_num;		/* Format number */
	};
	uint32 t;			/* Type cheat */
} event_log_hdr_t;

#endif /* _EVENT_LOG_TAG_H_ */
