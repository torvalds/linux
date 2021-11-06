/*
 * Dongle WL Header definitions
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

#ifndef _dngl_wlhdr_h_
#define _dngl_wlhdr_h_

typedef struct wl_header {
    uint8   type;           /* Header type */
    uint8   version;        /* Header version */
	int8	rssi;			/* RSSI */
	uint8	pad;			/* Unused */
} wl_header_t;

#define WL_HEADER_LEN   sizeof(wl_header_t)
#define WL_HEADER_TYPE  0
#define WL_HEADER_VER   1
#endif /* _dngl_wlhdr_h_ */
