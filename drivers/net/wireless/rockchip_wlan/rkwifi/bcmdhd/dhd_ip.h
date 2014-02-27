/*
 * Header file describing the common ip parser function.
 *
 * Provides type definitions and function prototypes used to parse ip packet.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id$
 */

#ifndef _dhd_ip_h_
#define _dhd_ip_h_

typedef enum pkt_frag
{
	DHD_PKT_FRAG_NONE = 0,
	DHD_PKT_FRAG_FIRST,
	DHD_PKT_FRAG_CONT,
	DHD_PKT_FRAG_LAST
} pkt_frag_t;

extern pkt_frag_t pkt_frag_info(osl_t *osh, void *p);

#endif /* _dhd_ip_h_ */
