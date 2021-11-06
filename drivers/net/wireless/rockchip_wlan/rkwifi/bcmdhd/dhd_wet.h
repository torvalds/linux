/*
 * Wireless Ethernet (WET) interface
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
 * $Id$
 */

/** XXX Twiki: [WirelessEthernet] */

#ifndef _dhd_wet_h_
#define _dhd_wet_h_

#include <ethernet.h>
#include <dngl_stats.h>
#include <dhd.h>

#define DHD_WET_ENAB 1
#define	WET_ENABLED(dhdp)	((dhdp)->info->wet_mode == DHD_WET_ENAB)

/* forward declaration */
typedef struct dhd_wet_info dhd_wet_info_t;

extern dhd_wet_info_t *dhd_get_wet_info(dhd_pub_t *pub);
extern void dhd_free_wet_info(dhd_pub_t *pub, void *wet);

/* Process frames in transmit direction */
extern int dhd_wet_send_proc(void *weth, void *sdu, void **new);
extern void dhd_set_wet_host_ipv4(dhd_pub_t *pub, void *parms, uint32 len);
extern void dhd_set_wet_host_mac(dhd_pub_t *pub, void *parms, uint32 len);
/* Process frames in receive direction */
extern int dhd_wet_recv_proc(void *weth, void *sdu);
extern void dhd_wet_sta_delete_list(dhd_pub_t *dhd_pub);

#ifdef PLC_WET
extern void dhd_wet_bssid_upd(dhd_wet_info_t *weth, dhd_bsscfg_t *cfg);
#endif /* PLC_WET */

int dhd_set_wet_mode(dhd_pub_t *dhdp, uint32 val);
int dhd_get_wet_mode(dhd_pub_t *dhdp);
extern void dhd_wet_dump(dhd_pub_t *dhdp, struct bcmstrbuf *b);

#endif	/* _dhd_wet_h_ */
