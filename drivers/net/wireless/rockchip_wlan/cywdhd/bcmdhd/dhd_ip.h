/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file describing the common ip parser function.
 *
 * Provides type definitions and function prototypes used to parse ip packet.
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
 * $Id: dhd_ip.h 537119 2015-02-25 04:24:14Z $
 */

#ifndef _dhd_ip_h_
#define _dhd_ip_h_

#ifdef DHDTCPACK_SUPPRESS
#include <dngl_stats.h>
#include <bcmutils.h>
#include <dhd.h>
#endif /* DHDTCPACK_SUPPRESS */

typedef enum pkt_frag
{
	DHD_PKT_FRAG_NONE = 0,
	DHD_PKT_FRAG_FIRST,
	DHD_PKT_FRAG_CONT,
	DHD_PKT_FRAG_LAST
} pkt_frag_t;

extern pkt_frag_t pkt_frag_info(osl_t *osh, void *p);

#ifdef DHDTCPACK_SUPPRESS
#define	TCPACKSZMIN	(ETHER_HDR_LEN + IPV4_MIN_HEADER_LEN + TCP_MIN_HEADER_LEN)
/* Size of MAX possible TCP ACK packet. Extra bytes for IP/TCP option fields */
#define	TCPACKSZMAX	(TCPACKSZMIN + 100)

/* Max number of TCP streams that have own src/dst IP addrs and TCP ports */
#define TCPACK_INFO_MAXNUM 4
#define TCPDATA_INFO_MAXNUM 4
#define TCPDATA_PSH_INFO_MAXNUM (16 * TCPDATA_INFO_MAXNUM)

#define TCPDATA_INFO_TIMEOUT 5000	/* Remove tcpdata_info if inactive for this time (in ms) */

#define DEFAULT_TCPACK_SUPP_RATIO 3
#ifndef CUSTOM_TCPACK_SUPP_RATIO
#define CUSTOM_TCPACK_SUPP_RATIO DEFAULT_TCPACK_SUPP_RATIO
#endif /* CUSTOM_TCPACK_SUPP_RATIO */

#define DEFAULT_TCPACK_DELAY_TIME 10 /* ms */
#ifndef CUSTOM_TCPACK_DELAY_TIME
#define CUSTOM_TCPACK_DELAY_TIME DEFAULT_TCPACK_DELAY_TIME
#endif /* CUSTOM_TCPACK_DELAY_TIME */

extern int dhd_tcpack_suppress_set(dhd_pub_t *dhdp, uint8 on);
extern void dhd_tcpack_info_tbl_clean(dhd_pub_t *dhdp);
extern int dhd_tcpack_check_xmit(dhd_pub_t *dhdp, void *pkt);
extern bool dhd_tcpack_suppress(dhd_pub_t *dhdp, void *pkt);
extern bool dhd_tcpdata_info_get(dhd_pub_t *dhdp, void *pkt);
extern bool dhd_tcpack_hold(dhd_pub_t *dhdp, void *pkt, int ifidx);
/* #define DHDTCPACK_SUP_DBG */
#if defined(DEBUG_COUNTER) && defined(DHDTCPACK_SUP_DBG)
extern counter_tbl_t tack_tbl;
#endif /* DEBUG_COUNTER && DHDTCPACK_SUP_DBG */
#endif /* DHDTCPACK_SUPPRESS */

#endif /* _dhd_ip_h_ */
