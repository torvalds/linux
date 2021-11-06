/* D11 macdbg function prototypes for Broadcom 802.11abgn
 * Networking Adapter Device Drivers.
 *
 * Broadcom Proprietary and Confidential. Copyright (C) 2020,
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
 * the contents of this file may not be disclosed to third parties,
 * copied or duplicated in any form, in whole or in part, without
 * the prior written permission of Broadcom.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 *
 * $Id: dhd_macdbg.h 649388 2016-07-15 22:54:42Z shinuk $
 */

#ifndef _dhd_macdbg_h_
#define _dhd_macdbg_h_
#ifdef BCMDBG
#include <dngl_stats.h>
#include <dhd.h>

extern int dhd_macdbg_attach(dhd_pub_t *dhdp);
extern void dhd_macdbg_detach(dhd_pub_t *dhdp);
extern void dhd_macdbg_event_handler(dhd_pub_t *dhdp, uint32 reason,
	uint8 *event_data, uint32 datalen);
extern int dhd_macdbg_dumpmac(dhd_pub_t *dhdp, char *buf, int buflen, int *outbuflen, bool dump_x);
extern int dhd_macdbg_pd11regs(dhd_pub_t *dhdp, char *params, int plen, char *buf, int buflen);
extern int dhd_macdbg_reglist(dhd_pub_t *dhdp, char *buf, int buflen);
extern int dhd_macdbg_dumpsvmp(dhd_pub_t *dhdp, char *buf, int buflen, int *outbuflen);
extern int dhd_macdbg_psvmpmems(dhd_pub_t *dhdp, char *params, int plen, char *buf, int buflen);
#endif /* BCMDBG */
#endif /* _dhd_macdbg_h_ */
