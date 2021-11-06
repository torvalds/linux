/*
 * Header file describing the common timesync functionality
 *
 * Provides type definitions and function prototypes used to handle timesync functionality.
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
 * $Id$:
 */

#ifndef _dhd_timesync_h_
#define _dhd_timesync_h_

typedef struct dhd_ts dhd_ts_t;

/* Linkage, sets "ts" link and updates hdrlen in pub */
extern int dhd_timesync_attach(dhd_pub_t *dhdp);

/* Linkage, sets "ts" link and updates hdrlen in pub */
extern bool dhd_timesync_watchdog(dhd_pub_t *dhdp);

/* Unlink, frees allocated timesync memory (including dhd_ts_t) */
extern int dhd_timesync_detach(dhd_pub_t *dhdp);

/* Check for and handle local prot-specific iovar commands */
extern int dhd_timesync_iovar_op(dhd_ts_t *ts, const char *name, void *params, int plen,
	void *arg, int len, bool set);

/* handle host time stamp completion */
extern void dhd_timesync_handle_host_ts_complete(dhd_ts_t *ts, uint16 xt_id, uint16 status);

/* handle fw time stamp event from firmware */
extern void dhd_timesync_handle_fw_timestamp(dhd_ts_t *ts, uchar *tlv, uint32 tlv_len,
	uint32 seqnum);

/* get notified of the ipc rev */
extern void dhd_timesync_notify_ipc_rev(dhd_ts_t *ts, uint32 ipc_rev);

/* log txs timestamps */
extern void dhd_timesync_log_tx_timestamp(dhd_ts_t *ts, uint16 flowid, uint8 intf,
	uint32 ts_low, uint32 ts_high, dhd_pkt_parse_t *parse);

/* log rx cpl timestamps */
extern void dhd_timesync_log_rx_timestamp(dhd_ts_t *ts, uint8 intf,
	uint32 ts_low, uint32 ts_high, dhd_pkt_parse_t *parse);

/* dynamically disabling it based on the host driver suspend/resume state */
extern void dhd_timesync_control(dhd_pub_t *dhdp, bool disabled);

extern void dhd_timesync_debug_info_print(dhd_pub_t *dhdp);
#endif /* _dhd_timesync_h_ */
