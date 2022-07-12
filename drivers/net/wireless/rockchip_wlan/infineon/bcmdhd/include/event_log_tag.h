/*
 * EVENT_LOG system definitions
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
 * $Id$
 */

#ifndef _EVENT_LOG_TAG_H_
#define _EVENT_LOG_TAG_H_

#include <typedefs.h>

/* Define new event log tags here */
#define EVENT_LOG_TAG_NULL	0	/* Special null tag */
#define EVENT_LOG_TAG_TS	1	/* Special timestamp tag */

/* HSIC Legacy support */
/* Possible candidates for reuse */
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

/* Possible candidates for reuse */
#define EVENT_LOG_TAG_UCODE_WATCHDOG 	26
#define EVENT_LOG_TAG_UCODE_FIFO 	27

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
/* EVENT_LOG_TAG_AWDL_TRACE_RANGING will be removed after wlc_ranging merge from IGUANA
 * keeping it here to avoid compilation error on trunk
 */
#define EVENT_LOG_TAG_AWDL_TRACE_RANGING	55
#define EVENT_LOG_TAG_RANGING_TRACE	55
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
#define EVENT_LOG_TAG_RSDB_PMGR_DEBUG		116
#define EVENT_LOG_TAG_RSDB_PMGR_ERR		117
#define EVENT_LOG_TAG_NAT_ERR                   118
#define EVENT_LOG_TAG_NAT_WARN                  119
#define EVENT_LOG_TAG_NAT_INFO                  120
#define EVENT_LOG_TAG_NAT_DEBUG                 121
#define EVENT_LOG_TAG_STA_INFO			122
#define EVENT_LOG_TAG_PROXD_ERROR		123
#define EVENT_LOG_TAG_PROXD_TRACE		124
#define EVENT_LOG_TAG_PROXD_INFO		125
#define EVENT_LOG_TAG_IE_ERROR			126
#define EVENT_LOG_TAG_ASSOC_ERROR		127
#define EVENT_LOG_TAG_SCAN_ERR			128
#define EVENT_LOG_TAG_AMSDU_ERROR		129
#define EVENT_LOG_TAG_AMPDU_ERROR		130
#define EVENT_LOG_TAG_KM_ERROR			131
#define EVENT_LOG_TAG_DFS			132
#define EVENT_LOG_TAG_REGULATORY		133
#define EVENT_LOG_TAG_CSA			134
#define EVENT_LOG_TAG_WNM_BSSTRANS_ERR		135
#define EVENT_LOG_TAG_SUP_INFO			136
#define EVENT_LOG_TAG_SUP_ERROR			137
#define EVENT_LOG_TAG_CHANCTXT_TRACE		138
#define EVENT_LOG_TAG_CHANCTXT_INFO		139
#define EVENT_LOG_TAG_CHANCTXT_ERROR		140
#define EVENT_LOG_TAG_CHANCTXT_WARN		141
#define EVENT_LOG_TAG_MSCHPROFILE		142
#define EVENT_LOG_TAG_4WAYHANDSHAKE		143
#define EVENT_LOG_TAG_MSCHPROFILE_TLV		144
#define EVENT_LOG_TAG_ADPS			145
#define EVENT_LOG_TAG_MBO_DBG			146
#define EVENT_LOG_TAG_MBO_INFO			147
#define EVENT_LOG_TAG_MBO_ERR			148
#define EVENT_LOG_TAG_TXDELAY			149
#define EVENT_LOG_TAG_BCNTRIM_INFO		150
#define EVENT_LOG_TAG_BCNTRIM_TRACE		151
#define EVENT_LOG_TAG_OPS_INFO			152
#define EVENT_LOG_TAG_STATS			153
#define EVENT_LOG_TAG_BAM			154
#define EVENT_LOG_TAG_TXFAIL			155
#define EVENT_LOG_TAG_AWDL_CONFIG_DBG		156
#define EVENT_LOG_TAG_AWDL_SYNC_DBG		157
#define EVENT_LOG_TAG_AWDL_PEER_DBG		158
#define EVENT_LOG_TAG_RANDMAC_INFO		159
#define EVENT_LOG_TAG_RANDMAC_DBG		160
#define EVENT_LOG_TAG_RANDMAC_ERR		161
#define EVENT_LOG_TAG_AWDL_DFSP_DBG		162
#define EVENT_LOG_TAG_MSCH_CAL			163
#define EVENT_LOG_TAG_MSCH_OPP_CAL		164
#define EVENT_LOG_TAG_MSCH			165
#define EVENT_LOG_TAG_NAN_SYNC			166
#define EVENT_LOG_TAG_NAN_DPE			167
#define EVENT_LOG_TAG_NAN_SCHED			168
#define EVENT_LOG_TAG_NAN_RNG			169
#define EVENT_LOG_TAG_NAN_DAM			170
#define EVENT_LOG_TAG_NAN_NA			171
#define EVENT_LOG_TAG_NAN_NDL			172
#define EVENT_LOG_TAG_NAN_NDP			173
#define EVENT_LOG_TAG_NAN_SEC			174
#define EVENT_LOG_TAG_NAN_MAC			175
#define EVENT_LOG_TAG_NAN_FSM			176

#define EVENT_LOG_TAG_TPA_ERR			192
#define EVENT_LOG_TAG_TPA_INFO			193
#define EVENT_LOG_TAG_OCE_DBG			194
#define EVENT_LOG_TAG_OCE_INFO			195
#define EVENT_LOG_TAG_OCE_ERR			196
#define EVENT_LOG_TAG_WL_WARN			197
#define EVENT_LOG_TAG_SB_ERR			198
#define EVENT_LOG_TAG_SB_INFO			199
#define EVENT_LOG_TAG_SB_SCHED			200
#define EVENT_LOG_TAG_ADPS_INFO			201
#define EVENT_LOG_TAG_SB_CMN_SYNC_INFO		202
#define EVENT_LOG_TAG_PHY_CAL_INFO		203 /* PHY CALs scheduler info */
#define EVENT_LOG_TAG_EVT_NOTIF_INFO		204
#define EVENT_LOG_TAG_PHY_HC_ERROR		205
#define EVENT_LOG_TAG_PHY_TXPWR_WARN		206
#define EVENT_LOG_TAG_PHY_TXPWR_INFO		207
#define EVENT_LOG_TAG_PHY_ACI_INFO		208
#define EVENT_LOG_TAG_WL_COUNTERS_AUX		209
#define EVENT_LOG_TAG_AMPDU_DUMP_AUX		210
#define EVENT_LOG_TAG_PWRSTATS_AWDL_AUX		211
#define EVENT_LOG_TAG_PWRSTATS_PHY_AUX		212
#define EVENT_LOG_TAG_PWRSTATS_SCAN_AUX		213
#define EVENT_LOG_TAG_PWRSTATS_WAKE_V2_AUX	214
#define EVENT_LOG_TAG_SVT_TESTING		215	/* SVT testing/verification */
#define EVENT_LOG_TAG_HND_SMD_ERROR		216
#define EVENT_LOG_TAG_PSBW_INFO			217
#define EVENT_LOG_TAG_PHY_CAL_DBG		218
#define EVENT_LOG_TAG_FILS_DBG			219
#define EVENT_LOG_TAG_FILS_INFO			220
#define EVENT_LOG_TAG_FILS_ERROR		221
#define EVENT_LOG_TAG_HWA_TXPOST		222
#define EVENT_LOG_TAG_HWA_TXDMA			223
/* Arbitrator callback log tags */
#define EVENT_LOG_TAG_STF_ARB_CB_TRACE		224
#define EVENT_LOG_TAG_STF_ARB_CB_ERROR		225
#define EVENT_LOG_TAG_PHY_PERIODIC_SEC		226

/* Debug tags for making debug builds */
#define EVENT_LOG_TAG_DBG1			251
#define EVENT_LOG_TAG_DBG2			252
#define EVENT_LOG_TAG_DBG3			253
#define EVENT_LOG_TAG_DBG4			254
#define EVENT_LOG_TAG_DBG5			255

/* Insert new tags here for Koala onwards */

/* NAN INFO/ERR evnt tags */
#define EVENT_LOG_TAG_NAN_SYNC_INFO             256
#define EVENT_LOG_TAG_NAN_DPE_INFO              257
#define EVENT_LOG_TAG_NAN_SCHED_INFO            258
#define EVENT_LOG_TAG_NAN_RNG_INFO              259
#define EVENT_LOG_TAG_NAN_DAM_INFO              260
#define EVENT_LOG_TAG_NAN_NA_INFO               261
#define EVENT_LOG_TAG_NAN_NDL_INFO              262
#define EVENT_LOG_TAG_NAN_NDP_INFO              263
#define EVENT_LOG_TAG_NAN_SEC_INFO              264
#define EVENT_LOG_TAG_NAN_MAC_INFO              265
#define EVENT_LOG_TAG_NAN_FSM_INFO              266
#define EVENT_LOG_TAG_NAN_PEER_INFO             267
#define EVENT_LOG_TAG_NAN_AVAIL_INFO            268
#define EVENT_LOG_TAG_NAN_CMN_INFO              269
#define EVENT_LOG_TAG_NAN_SYNC_ERR              270
#define EVENT_LOG_TAG_NAN_DPE_ERR               271
#define EVENT_LOG_TAG_NAN_SCHED_ERR             272
#define EVENT_LOG_TAG_NAN_RNG_ERR               273
#define EVENT_LOG_TAG_NAN_DAM_ERR               274
#define EVENT_LOG_TAG_NAN_NA_ERR                275
#define EVENT_LOG_TAG_NAN_NDL_ERR               276
#define EVENT_LOG_TAG_NAN_NDP_ERR               277
#define EVENT_LOG_TAG_NAN_SEC_ERR               278
#define EVENT_LOG_TAG_NAN_MAC_ERR               279
#define EVENT_LOG_TAG_NAN_FSM_ERR               280
#define EVENT_LOG_TAG_NAN_PEER_ERR              281
#define EVENT_LOG_TAG_NAN_AVAIL_ERR             282
#define EVENT_LOG_TAG_NAN_CMN_ERR               283

/* More NAN DBG evt Tags */
#define EVENT_LOG_TAG_NAN_PEER                  284
#define EVENT_LOG_TAG_NAN_AVAIL                 285
#define EVENT_LOG_TAG_NAN_CMN                   286

#define EVENT_LOG_TAG_SAE_ERROR			287
#define EVENT_LOG_TAG_SAE_INFO			288

/* rxsig module logging */
#define EVENT_LOG_TAG_RXSIG_ERROR               289
#define EVENT_LOG_TAG_RXSIG_DEBUG               290
#define EVENT_LOG_TAG_RXSIG_INFO                291

/* HE TWT HEB EVEVNT_LOG_TAG */
#define EVENT_LOG_TAG_WL_HE_INFO                292
#define EVENT_LOG_TAG_WL_HE_TRACE               293
#define EVENT_LOG_TAG_WL_HE_WARN                294
#define EVENT_LOG_TAG_WL_HE_ERROR               295
#define EVENT_LOG_TAG_WL_TWT_INFO               296
#define EVENT_LOG_TAG_WL_TWT_TRACE              297
#define EVENT_LOG_TAG_WL_TWT_WARN               298
#define EVENT_LOG_TAG_WL_TWT_ERROR              299
#define EVENT_LOG_TAG_WL_HEB_ERROR              300
#define EVENT_LOG_TAG_WL_HEB_TRACE              301

/* RRM EVENT_LOG_TAG */
#define EVENT_LOG_TAG_RRM_DBG                   302
#define EVENT_LOG_TAG_RRM_INFO                  303
#define EVENT_LOG_TAG_RRM_ERR                   304

/* scan core */
#define EVENT_LOG_TAG_SC			305

#define EVENT_LOG_TAG_ESP_DBG			306
#define EVENT_LOG_TAG_ESP_INFO			307
#define EVENT_LOG_TAG_ESP_ERR			308

/* SDC */
#define EVENT_LOG_TAG_SDC_DBG			309
#define EVENT_LOG_TAG_SDC_INFO			310
#define EVENT_LOG_TAG_SDC_ERR			311

/* EVENT_LOG_TAG_MAX	= Set to the same value of last tag, not last tag + 1 */
#define EVENT_LOG_TAG_MAX			312

typedef enum wl_el_set_type_def {
	EVENT_LOG_SET_TYPE_DEFAULT = 0, /* flush the log buffer when it is full - Default option */
	EVENT_LOG_SET_TYPE_PRSRV = 1, /* flush the log buffer based on fw or host trigger */
	EVENT_LOG_SET_TYPE_DFLUSH = 2 /* flush the log buffer once the watermark is reached */
} wl_el_set_type_def_t;

#define EVENT_LOG_TAG_FLUSH_NONE		0x00	/* No flush */
#define EVENT_LOG_TAG_FLUSH_ALL			0x40	/* Flush all preserved sets */
#define EVENT_LOG_TAG_FLUSH_SETNUM		0x80	/* Flush preserved set */
#define EVENT_LOG_TAG_FLUSH_MASK		0x3f	/* SetNum Mask */

typedef enum wl_el_flush_type {
	EL_TAG_PRSRV_FLUSH_NONE = 0,	/* No flush of preserve buf on this tag */
	EL_TAG_PRSRV_FLUSH_SETNUM,	/* Flush the buffer set specifid on this tag */
	EL_TAG_PRSRV_FLUSH_ALL		/* Flush all preserved buffer set on this tag */
} wl_el_flush_type_t;

#define EVENT_LOG_FLUSH_CURRENT_VERSION 0
typedef struct wl_el_set_flush_prsrv_s {
	uint16	version;
	uint16	len;
	uint16	tag;		/* Tag for which preserve flush should be done */
	uint8	flush_type;	/* Check wl_el_flush_type_t */
	uint8	set_num;	/* Log set num to flush. Max is NUM_EVENT_LOG_SETS. Valid only when
				 * action is EVENT_LOG_TAG_FLUSH_SETNUM
				 */
} wl_el_set_flush_prsrv_t;

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

/* for internal use - legacy max. tag */
#define EVENT_LOG_TAG_MAX_LEGACY_FORMAT		255

/*
 * The position of the extended header in the event log stream will be as follows:
 * <event log payload><ARM cycle count timestamp><extended header><regular header>
 * Extended header could be due to count > 255 or tag > 255.
 *
 * Extended count: 6 bits long. 8 bits (existing) + 6 bits =>
 * 2^14 words = 65536 bytes payload max
 * Extended count field is currently reserved
 * Extended tag: 8 (existing) + 4 bits = 12 bits =>2^12 = 4096 tags
 * bits[7..4] of extended tags are reserved.
 * MSB 16 bits of the extended header are reserved for future use.
 */

typedef union event_log_extended_hdr {
	struct {
		uint8 extended_tag; /* Extended tag, bits[7..4] are reserved */
		uint8 extended_count; /* Extended count. Reserved for now. */
		uint16 rsvd;	/* Reserved */
	};

	uint32 t;	/* Type cheat */
} event_log_extended_hdr_t;
#endif /* _EVENT_LOG_TAG_H_ */
