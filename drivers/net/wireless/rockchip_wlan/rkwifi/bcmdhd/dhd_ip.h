/*
 * Header file describing the common ip parser function.
 *
 * Provides type definitions and function prototypes used to parse ip packet.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: dhd_ip.h 502735 2014-09-16 00:53:02Z $
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
extern bool pkt_is_dhcp(osl_t *osh, void *p);

#ifdef DHDTCPACK_SUPPRESS
#define	TCPACKSZMIN	(ETHER_HDR_LEN + IPV4_MIN_HEADER_LEN + TCP_MIN_HEADER_LEN)
/* Size of MAX possible TCP ACK packet. Extra bytes for IP/TCP option fields */
#define	TCPACKSZMAX	(TCPACKSZMIN + 100)

/* Max number of TCP streams that have own src/dst IP addrs and TCP ports */
#define TCPACK_INFO_MAXNUM 4
#define TCPDATA_INFO_MAXNUM 4
#define TCPDATA_PSH_INFO_MAXNUM (8 * TCPDATA_INFO_MAXNUM)

#define TCPDATA_INFO_TIMEOUT 5000	/* Remove tcpdata_info if inactive for this time (in ms) */

#define TCPACK_SUPP_RATIO 3
#define TCPACK_DELAY_TIME 10 /* ms */

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
