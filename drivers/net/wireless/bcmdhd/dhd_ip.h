/*
 * Header file describing the common ip parser function.
 *
 * Provides type definitions and function prototypes used to parse ip packet.
 *
 * Copyright (C) 1999-2013, Broadcom Corporation
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
